/**
 * This file is part of the CernVM File System.
 */

#ifndef CVMFS_UPLOAD_FACILITY_H_
#define CVMFS_UPLOAD_FACILITY_H_

#include <fcntl.h>

#include <string>

#include "ingestion/task.h"
#include "ingestion/tube.h"
#include "upload_spooler_definition.h"
#include "util/posix.h"
#include "util_concurrency.h"

namespace upload {

struct UploaderResults {
  enum Type { kFileUpload, kBufferUpload, kChunkCommit };

  UploaderResults(const int return_code, const std::string &local_path)
    : type(kFileUpload),
      return_code(return_code),
      local_path(local_path) {}

  explicit UploaderResults(Type t, const int return_code)
    : type(t),
      return_code(return_code),
      local_path("") {}

  const Type type;
  const int return_code;
  const std::string local_path;
};

struct UploadStreamHandle;

/**
 * Abstract base class for all backend upload facilities
 * This class defines an interface and constructs the concrete Uploaders,
 * futhermore it handles callbacks to the outside world to notify users of done
 * upload jobs.
 *
 * Note: Users could be both the Spooler (when calling Spooler::Upload()) and
 *       the IngestionPipeline (when calling Spooler::Process()). We therefore
 *       cannot use the Observable template here, since this would forward
 *       finished upload jobs to ALL listeners instead of only the owner of the
 *       specific job.
 */
class AbstractUploader
  : public PolymorphicConstruction<AbstractUploader, SpoolerDefinition>
  , public Callbackable<UploaderResults>
{
  friend class TaskUpload;

 public:
  /**
   * A read-only memory block that is supposed to be written out.
   */
  struct UploadBuffer {
    UploadBuffer() : size(0), data(NULL) { }
    UploadBuffer(uint64_t s, void *d) : size(s), data(d) { }
    uint64_t size;
    void *data;
  };

  struct JobStatus {
    enum State { kOk, kTerminate, kNoJobs };
  };

  struct UploadJob {
    enum Type { Upload, Commit, Terminate };

    UploadJob(UploadStreamHandle *handle, UploadBuffer buffer,
              const CallbackTN *callback = NULL)
        : type(Upload),
          stream_handle(handle),
          buffer(buffer),
          callback(callback) {}

    UploadJob(UploadStreamHandle *handle, const shash::Any &content_hash)
        : type(Commit),
          stream_handle(handle),
          buffer(),
          callback(NULL),
          content_hash(content_hash) {}

    UploadJob()
        : type(Terminate), stream_handle(NULL), buffer(), callback(NULL) {}

    static UploadJob *CreateQuitBeacon() { return new UploadJob(); }
    bool IsQuitBeacon() { return type == Terminate; }

    Type type;
    UploadStreamHandle *stream_handle;

    // type==Upload specific fields
    UploadBuffer buffer;
    const CallbackTN *callback;

    // type==Commit specific fields
    shash::Any content_hash;
  };

  virtual ~AbstractUploader() { assert(!tasks_upload_.is_active()); }

  /**
   * A string identifying the uploader type
   */
  virtual std::string name() const = 0;

  /**
   * Concrete uploaders might want to use more tasks for writing, for instance
   * one per disk.
   */
  virtual unsigned GetNumTasks() const { return 1; }

  /**
   * This is called right after the constructor of AbstractUploader or/and its
   * derived class has been executed. You can override that to do additional
   * initialization that cannot be done in the constructor itself.
   *
   * @return   true on successful initialization
   */
  virtual bool Initialize();

  /**
   * Called during Spooler::WaitForUpload(), to ensure that the upload has
   * finished. If commit == true, then a Commit request is also sent, to apply
   * all the the changes accumulated during the session. "catalog_path"
   * represents the path of the root catalog with the changes.
   * By default it is a noop and returns true;
   */
  virtual bool FinalizeSession(bool commit, const std::string &old_root_hash,
                               const std::string &new_root_hash);

  /**
   * This must be called right before the destruction of the AbstractUploader!
   * You are _not_ supposed to overwrite this method in your concrete Uploader.
   */
  void TearDown();

  /**
   * Uploads the file at the path local_path into the backend storage under the
   * path remote_path. When the upload has finished it calls callback.
   * Note: This method might be implemented in a synchronous way.
   *
   * @param local_path   path to the file to be uploaded
   * @param remote_path  desired path for the file in the backend storage
   * @param callback     (optional) gets notified when the upload was finished
   */
  void Upload(
    const std::string &local_path,
    const std::string &remote_path,
    const CallbackTN *callback = NULL)
  {
    ++jobs_in_flight_;
    FileUpload(local_path, remote_path, callback);
  }

  /**
   * This method is called before the first data block of a streamed upload is
   * scheduled (see above implementation of UploadStreamHandle for details).
   *
   * @param callback   (optional) this callback will be invoked once this parti-
   *                   cular streamed upload is committed.
   * @return           a pointer to the initialized UploadStreamHandle
   */
  virtual UploadStreamHandle *InitStreamedUpload(
      const CallbackTN *callback = NULL) = 0;

  /**
   * This method schedules a buffer to be uploaded in the context of the
   * given UploadStreamHandle. The actual upload will happen asynchronously by
   * a concrete implementation of AbstractUploader
   * (see AbstractUploader::StreamedUpload()).
   * As soon has the scheduled upload job is complete (either successful or not)
   * the optionally passed callback is supposed to be invoked using
   * AbstractUploader::Respond().
   *
   * @param handle    Pointer to a previously acquired UploadStreamHandle
   * @param buffer    contains the data block to be uploaded
   * @param callback  (optional) callback object to be invoked once the given
   *                  upload is finished (see AbstractUploader::Respond())
   */
  void ScheduleUpload(
    UploadStreamHandle *handle,
    UploadBuffer buffer,
    const CallbackTN *callback = NULL)
  {
    ++jobs_in_flight_;
    tube_upload_.Enqueue(new UploadJob(handle, buffer, callback));
  }

  /**
   * This method schedules a commit job as soon as all data blocks of a streamed
   * upload are (successfully) uploaded. Derived classes must override
   * AbstractUploader::FinalizeStreamedUpload() for this to happen.
   *
   * @param handle        Pointer to a previously acquired UploadStreamHandle
   * @param content_hash  the content hash of the full uploaded data Chunk
   */
  void ScheduleCommit(
    UploadStreamHandle *handle,
    const shash::Any &content_hash)
  {
    ++jobs_in_flight_;
    tube_upload_.Enqueue(new UploadJob(handle, content_hash));
  }

  /**
   * Removes a file from the backend storage. This might be done synchronously.
   *
   * Note: If the file doesn't exist before calling this method it will report
   *       a successful deletion anyways.
   *
   * Note: This method is currently used very sparsely! If this changes in the
   *       future, one might think about doing deletion asynchronously!
   *
   * @param file_to_delete  path to the file to be removed
   * @return                true if the file does not exist (anymore), false if
   *                        the removal failed
   */
  virtual bool Remove(const std::string &file_to_delete) = 0;

  /**
   * Overloaded Remove method used to remove a object based on its content hash.
   *
   * @param hash_to_delete  the content hash of a file to be deleted
   * @return                true on successful removal (removing a non-existant
   *                        object is a successful deletion as well!)
   */
  virtual bool Remove(const shash::Any &hash_to_delete) {
    return Remove("data/" + hash_to_delete.MakePath());
  }

  /**
   * Checks if a file is already present in the backend storage. This might be a
   * synchronous operation.
   *
   * @param path  the path of the file to be checked
   * @return      true if the file was found in the backend storage
   */
  virtual bool Peek(const std::string &path) const = 0;

  /**
   * Creates a top-level shortcut to the given data object. This is particularly
   * useful for bootstrapping repositories whose data-directory is secured by
   * a VOMS certificate.
   *
   * @param object  content hash of the object to be exposed on the top-level
   * @return        true on success
   */
  virtual bool PlaceBootstrappingShortcut(const shash::Any &object) const = 0;

  /**
   * Waits until the current upload queue is empty.
   *
   * Note: This does NOT necessarily mean, that all files are actuall uploaded.
   *       If new jobs are concurrently scheduled the behavior of this method is
   *       not defined (it returns also on intermediately empty queues)
   */
  virtual void WaitForUpload() const;

  virtual unsigned int GetNumberOfErrors() const = 0;
  static void RegisterPlugins();

 protected:
  typedef Callbackable<UploaderResults>::CallbackTN *CallbackPtr;

  explicit AbstractUploader(const SpoolerDefinition &spooler_definition);

  /**
   * Implementation of plain file upload
   * Public interface: AbstractUploader::Upload()
   *
   * @param local_path   file to be uploaded
   * @param remote_path  destination to be written in the backend
   * @param callback     callback to be called on completion
   */
  virtual void FileUpload(const std::string &local_path,
                          const std::string &remote_path,
                          const CallbackTN *callback = NULL) = 0;

  /**
   * Implementation of a streamed upload step. See public interface for details.
   * Public interface: AbstractUploader::ScheduleUpload()
   *
   * @param handle     decendant of UploadStreamHandle specifying the stream
   * @param buffer     the CharBuffer to be uploaded to the stream
   * @param callback   callback to be called on completion
   */
  virtual void StreamedUpload(UploadStreamHandle *handle,
                              UploadBuffer buffer,
                              const CallbackTN *callback) = 0;

  /**
   * Implemetation of streamed upload commit
   * Public interface: AbstractUploader::ScheduleUpload()
   *
   * @param handle        decendant of UploadStreamHandle specifying the stream
   * @param content_hash  the computed content hash of the streamed object
   */
  virtual void FinalizeStreamedUpload(UploadStreamHandle *handle,
                                      const shash::Any &content_hash) = 0;

  /**
   * This notifies the callback that is associated to a finishing job. Please
   * do not call the handed callback yourself in concrete Uploaders!
   *
   * Note: Since the job is finished after we respond to it, the callback object
   *       gets automatically destroyed by this call!
   *       Therefore you must not call Respond() twice or use the callback later
   *       by any means!
   */
  void Respond(const CallbackTN *callback,
               const UploaderResults &result) const
  {
    if (callback != NULL) {
      (*callback)(result);
      delete callback;
    }

    --jobs_in_flight_;
  }

  /**
   * Creates a temporary file in the backend storage's temporary location
   * For the LocalUploader this usually is the 'txn' directory of the backend
   * storage. Otherwise it is some scratch area.
   *
   * @param path   pointer to a string that will contain the created file path
   * @return       a file descriptor to the opened file
   */
  int CreateAndOpenTemporaryChunkFile(std::string *path) const;

  const SpoolerDefinition &spooler_definition() const {
    return spooler_definition_;
  }

 private:
  const SpoolerDefinition spooler_definition_;

  mutable SynchronizingCounter<int32_t> jobs_in_flight_;
  TubeConsumerGroup<UploadJob> tasks_upload_;
  Tube<UploadJob> tube_upload_;
};  // class AbstractUploader


/**
 * The actual writing is multi-threaded.
 */
class TaskUpload : public TubeConsumer<AbstractUploader::UploadJob> {
 public:
  explicit TaskUpload(AbstractUploader *uploader)
    : TubeConsumer<AbstractUploader::UploadJob>(&(uploader->tube_upload_))
    , uploader_(uploader)
  { }

 protected:
  virtual void Process(AbstractUploader::UploadJob *upload_job);

 private:
  AbstractUploader *uploader_;
};


/**
 * Each implementation of AbstractUploader must provide its own derivate of the
 * UploadStreamHandle that is supposed to contain state information for the
 * streamed upload of one specific chunk.
 * Each UploadStreamHandle contains a callback object that is invoked as soon as
 * the streamed upload is committed.
 */
struct UploadStreamHandle {
  typedef AbstractUploader::CallbackTN CallbackTN;

  explicit UploadStreamHandle(const CallbackTN *commit_callback)
      : commit_callback(commit_callback) {}
  virtual ~UploadStreamHandle() {}

  const CallbackTN *commit_callback;
};

}  // namespace upload

#endif  // CVMFS_UPLOAD_FACILITY_H_
