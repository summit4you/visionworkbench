// __BEGIN_LICENSE__
// Copyright (C) 2006-2009 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


#include <vw/Plate/BlobManager.h>
#include <vw/Plate/Exception.h>

// Vision Workbench
#include <vw/Core/Exception.h>
#include <vw/Core/FundamentalTypes.h>
#include <vw/Core/Log.h>

// -------------------------------------------------------------------
//                            BLOB_MANAGER
// -------------------------------------------------------------------

// Move to the next blob, wrapping around if we reach the end.
void vw::platefile::BlobManager::increment_blob_index(int &blob_index) {
  ++blob_index;
  if (blob_index >= m_blob_locks.size())
    blob_index = 0;
}

// A method to poll for an available blob.  Returns -1 if there
// are no blobs available.  
int vw::platefile::BlobManager::get_next_available_blob() {

  // Move the starting point for our search forward one so that we
  // don't always return the same general set of blobs.
  increment_blob_index(m_blob_index);

  // If the next blob_id happens to be unlocked and not full, then we
  // return it immediately.
  if ( !(m_blob_locks[m_blob_index].locked) && 
       m_blob_locks[m_blob_index].current_blob_offset < m_max_blob_size) 
    return m_blob_index;

  // If not, then we neet to search.  Set the starting point so that
  // we can tell when we've wrapped all the way around..
  int starting_blob = m_blob_index;
  increment_blob_index(m_blob_index);

  while (m_blob_index != starting_blob) {

    // If we find a blob that is both unlocked and not full, then we
    // return its ID.
    if ( !(m_blob_locks[m_blob_index].locked) && 
         m_blob_locks[m_blob_index].current_blob_offset < m_max_blob_size) {
      return m_blob_index;
    }

    // Otherwise, we increment m_blob_index and try again.
    increment_blob_index(m_blob_index);
  }

  // If we have reached this point, then no valid blobs were found.
  // They must all be full or locked.  If that's the case, then we
  // create a new one here, stick it on the end, and return that new
  // blob_id.  (Unless we have reached max_blobs, in which case we
  // return -1.)
  if (m_blob_locks.size() >= m_max_blobs) {
    return -1;
  } else {
    BlobRecord rec;
    rec.locked = false;
    rec.current_blob_offset = 0;
    m_blob_locks.push_back(rec);
    return m_blob_locks.size() - 1;
  }
}

/// Create a new blob manager.  The max_blob_size is specified in
/// units of megabytes.
vw::platefile::BlobManager::BlobManager(int64 max_blob_size, int initial_nblobs, int max_blobs) : 
  m_max_blob_size(max_blob_size * 1024 * 1024), m_max_blobs(max_blobs), m_blob_index(0) { 

  VW_ASSERT(initial_nblobs >=1, ArgumentErr() << "BlobManager: inital_nblobs must be >= 1.");

  // Initialize the blob locks.  Set them all to 'unlocked'
  // (i.e. false)
  m_blob_locks.resize(initial_nblobs);
  for (unsigned i=0; i < m_blob_locks.size(); ++i) {
    m_blob_locks[i].current_blob_offset = 0;
    m_blob_locks[i].locked = false;
  }
}

/// Return the number of blobs currently in use.
unsigned vw::platefile::BlobManager::num_blobs() {
  Mutex::Lock lock(m_mutex);
  return m_blob_locks.size();
}

vw::int64 vw::platefile::BlobManager::max_blob_size() { 
  Mutex::Lock lock(m_mutex);
  return m_max_blob_size; 
}

/// Request a blob to write to that has sufficient space to write at
/// least 'size' bytes.  Returns the blob index of a locked blob
/// that you have sole access to write to.
///
/// size is specified in bytes.
//
// TODO: This is pretty simple logic, and would not be very
// efficient because it blocks on write if it catches up to a blob
// that is still locked.  We should add real blob selection logic
// here at a later date.
int vw::platefile::BlobManager::request_lock(int64 size) {
  Mutex::Lock lock(m_mutex);
 
  // First, we check to see if the next blob is free.  If not, we
  // wait for a release event and recheck.
  int next_available_blob = get_next_available_blob();
  if (next_available_blob == -1)
    vw_throw(BlobLimitErr() << "Unable to create more blob files. " 
             << "The blob limit has been reached.");
  
  // Then we lock it, increment the blob index, and return it.
  m_blob_locks[next_available_blob].locked = true;  
  return next_available_blob;
}

// Release the blob lock and update its write index (essentially
// "committing" the write to the blob when you are finished with it.).
void vw::platefile::BlobManager::release_lock(int blob_id, uint64 blob_offset) {
  Mutex::Lock lock(m_mutex);
  m_blob_locks[blob_id].current_blob_offset = blob_offset;
  m_blob_locks[blob_id].locked = false;
  m_blob_release_condition.notify_all();
}

