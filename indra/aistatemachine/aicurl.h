/**
 * @file aicurl.h
 * @brief Thread safe wrapper for libcurl.
 *
 * Copyright (c) 2012, Aleric Inglewood.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution.
 *
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   17/03/2012
 *   Initial version, written by Aleric Inglewood @ SL
 */

#ifndef AICURL_H
#define AICURL_H

#include <string>
#include <vector>
#include <set>
#include <stdexcept>
#include <boost/intrusive_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>

#include "llpreprocessor.h"
#include <curl/curl.h>		// Needed for files that include this header (also for aicurlprivate.h).
#ifdef DEBUG_CURLIO
#include "debug_libcurl.h"
#endif

// Make sure we don't use this option: it is not thread-safe.
#undef CURLOPT_DNS_USE_GLOBAL_CACHE
#define CURLOPT_DNS_USE_GLOBAL_CACHE do_not_use_CURLOPT_DNS_USE_GLOBAL_CACHE

#include "stdtypes.h"		// U16, S32, U32, F64
#include "llatomic.h"		// LLAtomicU32
#include "aithreadsafe.h"
#include "llhttpstatuscodes.h"
#include "aihttpheaders.h"

extern bool gNoVerifySSLCert;

class LLSD;
class LLBufferArray;
class LLChannelDescriptors;
class AIHTTPTimeoutPolicy;

// Some pretty printing for curl easy handle related things:
// Print the lock object related to the current easy handle in every debug output.
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#include <sstream>
#define DoutCurl(x) do { \
	using namespace libcwd; \
	std::ostringstream marker; \
	marker << (void*)this->get_lockobj(); \
	libcw_do.push_marker(); \
	libcw_do.marker().assign(marker.str().data(), marker.str().size()); \
	libcw_do.inc_indent(2); \
	Dout(dc::curl, x); \
	libcw_do.dec_indent(2); \
	libcw_do.pop_marker(); \
  } while(0)
#define DoutCurlEntering(x) do { \
	using namespace libcwd; \
	std::ostringstream marker; \
	marker << (void*)this->get_lockobj(); \
	libcw_do.push_marker(); \
	libcw_do.marker().assign(marker.str().data(), marker.str().size()); \
	libcw_do.inc_indent(2); \
	DoutEntering(dc::curl, x); \
	libcw_do.dec_indent(2); \
	libcw_do.pop_marker(); \
  } while(0)
#else // !CWDEBUG
#define DoutCurl(x) Dout(dc::curl, x << " [" << (void*)this->get_lockobj() << ']')
#define DoutCurlEntering(x) DoutEntering(dc::curl, x << " [" << (void*)this->get_lockobj() << ']')
#endif // CWDEBUG

//-----------------------------------------------------------------------------
// Exceptions.
//

// A general curl exception.
//
class AICurlError : public std::runtime_error {
  public:
	AICurlError(std::string const& message) : std::runtime_error(message) { }
};

class AICurlNoEasyHandle : public AICurlError {
  public:
	AICurlNoEasyHandle(std::string const& message) : AICurlError(message) { }
};

class AICurlNoMultiHandle : public AICurlError {
  public:
	AICurlNoMultiHandle(std::string const& message) : AICurlError(message) { }
};

class AICurlNoBody : public AICurlError {
  public:
	AICurlNoBody(std::string const& message) : AICurlError(message) { }
};

// End Exceptions.
//-----------------------------------------------------------------------------

// Forward declaration.
namespace AICurlInterface { struct TransferInfo; }

// Events generated by AICurlPrivate::CurlResponderBuffer.
struct AICurlResponderBufferEvents {
	virtual void received_HTTP_header(void) = 0;										// For example "HTTP/1.0 200 OK", the first header of a reply.
	virtual void received_header(std::string const& key, std::string const& value) = 0;	// Subsequent headers.
	virtual void completed_headers(U32 status, std::string const& reason, CURLcode code, AICurlInterface::TransferInfo* info) = 0;	// Transaction completed.
};

// Things defined in this namespace are called from elsewhere in the viewer code.
namespace AICurlInterface {

// Output parameter of AICurlPrivate::CurlEasyRequest::getResult.
// Used in XMLRPCResponder.
struct TransferInfo {
  TransferInfo() : mSizeDownload(0.0), mTotalTime(0.0), mSpeedDownload(0.0) { }
  F64 mSizeDownload;
  F64 mTotalTime;
  F64 mSpeedDownload;
};

//-----------------------------------------------------------------------------
// Global functions.

// Called once at start of application (from newview/llappviewer.cpp by main thread (before threads are created)),
// with main purpose to initialize curl.
void initCurl(void (*)(void) = NULL);

// Called once at start of application (from LLAppViewer::initThreads), starts AICurlThread.
void startCurlThread(U32 CurlConcurrentConnections, bool NoVerifySSLCert);

// Called once at end of application (from newview/llappviewer.cpp by main thread),
// with purpose to stop curl threads, free curl resources and deinitialize curl.
void cleanupCurl(void);

// Called from indra/llmessage/llurlrequest.cpp to print debug output regarding
// an error code returned by EasyRequest::getResult.
// Just returns curl_easy_strerror(errorcode).
std::string strerror(CURLcode errorcode);

// Called from indra/newview/llfloaterabout.cpp for the About floater, and
// from newview/llappviewer.cpp in behalf of debug output.
// Just returns curl_version().
std::string getVersionString(void);

// Called from newview/llappviewer.cpp (and llcrashlogger/llcrashlogger.cpp) to set
// the Certificate Authority file used to verify HTTPS certs.
void setCAFile(std::string const& file);

// Not called from anywhere.
// Can be used to set the path to the Certificate Authority file.
void setCAPath(std::string const& file);

//-----------------------------------------------------------------------------
// Global classes.

// Responder - base class for Request::get* and Request::post API.
//
// The life cycle of classes derived from this class is as follows:
// They are allocated with new on the line where get(), getByteRange() or post() is called,
// and the pointer to the allocated object is then put in a reference counting ResponderPtr.
// This ResponderPtr is passed to CurlResponderBuffer::prepRequest which stores it in its
// member mResponder. Hence, the life time of a Responder is never longer than its
// associated CurlResponderBuffer, however, if everything works correctly, then normally a
// responder is deleted in CurlResponderBuffer::removed_from_multi_handle by setting
// mReponder to NULL.
//
// Note that the lifetime of CurlResponderBuffer is (a bit) shorter than the associated
// CurlEasyRequest (because of the order of base classes of ThreadSafeBufferedCurlEasyRequest)
// and the callbacks, as set by prepRequest, only use those two.
// A callback locks the CurlEasyRequest before actually making the callback, and the
// destruction of CurlResponderBuffer also first locks the CurlEasyRequest, and then revokes
// the callbacks. This assures that a Responder is never used when the objects it uses are
// destructed. Also, if any of those are destructed then the Responder is automatically
// destructed too.
//
class Responder : public AICurlResponderBufferEvents {
  public:
	typedef boost::shared_ptr<LLBufferArray> buffer_ptr_t;

  protected:
	Responder(void);
	virtual ~Responder();

  protected:
	// Associated URL, used for debug output.
	std::string mURL;

	// Headers received from the server.
	AIHTTPReceivedHeaders mReceivedHeaders;

	// Set when the transaction finished (with or without errors).
	bool mFinished;

  public:
	// Called to set the URL of the current request for this Responder,
	// used only when printing debug output regarding activity of the Responder.
	void setURL(std::string const& url);

	// Accessor.
	std::string const& getURL(void) const { return mURL; }

	// Called by CurlResponderBuffer::timed_out or CurlResponderBuffer::processOutput.
	void finished(U32 status, std::string const& reason, LLChannelDescriptors const& channels, buffer_ptr_t const& buffer)
	{
	  completedRaw(status, reason, channels, buffer);
	  mFinished = true;
	}

	// Return true if the curl thread is done with this transaction.
	// If this returns true then it is guaranteed that none of the
	// virtual functions will be called anymore: the curl thread
	// will not access this object anymore.
	// Note that normally you don't need to call this function.
	bool is_finished(void) const { return mFinished; }

  protected:
	// Called when the "HTTP/1.x <status> <reason>" header is received.
	/*virtual*/ void received_HTTP_header(void)
	{
	  // It's possible that this page was moved (302), so we already saw headers
	  // from the 302 page and are starting over on the new page now.
	  mReceivedHeaders.clear();
	}

	// Called for all remaining headers.
	/*virtual*/ void received_header(std::string const& key, std::string const& value)
	{
	  mReceivedHeaders.addHeader(key, value);
	}

	// Called when the whole transaction is completed (also the body was received), but before the body is processed.
	/*virtual*/ void completed_headers(U32 status, std::string const& reason, CURLcode code, TransferInfo* info)
	{
	  completedHeaders(status, reason, mReceivedHeaders);
	}

  public:
	// Derived classes that implement completed_headers()/completedHeaders() should return true here.
	virtual bool needsHeaders(void) const { return false; }

	// Timeout policy to use.
	virtual AIHTTPTimeoutPolicy const& getHTTPTimeoutPolicy(void) const = 0;

	// A derived class should return true if curl should follow redirections.
	// The default is not to follow redirections.
	virtual bool followRedir(void) { return false; }

  protected:
	// Derived classes can override this to get the HTML headers that were received, when the message is completed.
	virtual void completedHeaders(U32 status, std::string const& reason, AIHTTPReceivedHeaders const& headers)
	{
	  // The default does nothing.
	}

	// Derived classes can override this to get the raw data of the body of the HTML message that was received.
	// The default is to interpret the content as LLSD and call completed().
	virtual void completedRaw(U32 status, std::string const& reason, LLChannelDescriptors const& channels, buffer_ptr_t const& buffer);

	// ... or, derived classes can override this to get the LLSD content when the message is completed.
	// The default is to call result() (or errorWithContent() in case of a HTML status indicating an error).
	virtual void completed(U32 status, std::string const& reason, LLSD const& content);

	// ... or, derived classes can override this to received the content of a body upon success.
	// The default does nothing.
	virtual void result(LLSD const& content);

	// Derived classes can override this to get informed when a bad HTML status code is received.
	// The default calls error().
	virtual void errorWithContent(U32 status, std::string const& reason, LLSD const& content);

	// ... or, derived classes can override this to get informed when a bad HTML status code is received.
	// The default prints the error to llinfos.
	virtual void error(U32 status, std::string const& reason);

  public:
	// Called from LLSDMessage::ResponderAdapter::listener.
	// LLSDMessage::ResponderAdapter is a hack, showing among others by fact that these functions need to be public.

	void pubErrorWithContent(U32 status, std::string const& reason, LLSD const& content) { errorWithContent(status, reason, content); }
	void pubResult(LLSD const& content) { result(content); }

  private:
	// Used by ResponderPtr. Object is deleted when reference count reaches zero.
	LLAtomicU32 mReferenceCount;

	friend void intrusive_ptr_add_ref(Responder* p);		// Called by boost::intrusive_ptr when a new copy of a boost::intrusive_ptr<Responder> is made.
	friend void intrusive_ptr_release(Responder* p);		// Called by boost::intrusive_ptr when a boost::intrusive_ptr<Responder> is destroyed.
															// This function must delete the Responder object when the reference count reaches zero.
};

// A Responder is passed around as ResponderPtr, which causes it to automatically
// destruct when there are no pointers left pointing to it.
typedef boost::intrusive_ptr<Responder> ResponderPtr;

// Same as above except that this class stores the result, allowing old polling
// code to poll if the transaction finished by calling is_finished() (from the
// main the thread) and then access the results-- as opposed to immediately
// digesting the results when any of the virtual functions are called.
class LegacyPolledResponder : public Responder {
  protected:
	CURLcode mCode;
	U32 mStatus;
	std::string mReason;

  public:
	LegacyPolledResponder(void) : mCode(CURLE_FAILED_INIT), mStatus(HTTP_INTERNAL_ERROR) { }

	// Accessors.
	CURLcode result_code(void) const { return mCode; }
	U32 http_status(void) const { return mStatus; }
	std::string const& reason(void) const { return mReason; }

	/*virtual*/ bool needsHeaders(void) const { return true; }
	/*virtual*/ void completed_headers(U32 status, std::string const& reason, CURLcode code, AICurlInterface::TransferInfo* info);
};

} // namespace AICurlInterface

// Forward declaration (see aicurlprivate.h).
namespace AICurlPrivate {
  class CurlEasyRequest;
} // namespace AICurlPrivate

// Define access types (_crat = Const Read Access Type, _rat = Read Access Type, _wat = Write Access Type).
// Typical usage is:
// AICurlEasyRequest h1;				// Create easy handle.
// AICurlEasyRequest h2(h1);			// Make lightweight copies.
// AICurlEasyRequest_wat h2_w(*h2);	// Lock and obtain write access to the easy handle.
// Use *h2_w, which is a reference to the locked CurlEasyRequest instance.
// Note: As it is not allowed to use curl easy handles in any way concurrently,
// read access would at most give access to a CURL const*, which will turn out
// to be completely useless; therefore it is sufficient and efficient to use
// an AIThreadSafeSimple and it's unlikely that AICurlEasyRequest_rat will be used.
typedef AIAccessConst<AICurlPrivate::CurlEasyRequest> AICurlEasyRequest_rat;
typedef AIAccess<AICurlPrivate::CurlEasyRequest> AICurlEasyRequest_wat;

// Events generated by AICurlPrivate::CurlEasyHandle.
struct AICurlEasyHandleEvents {
	// Events.
	virtual void added_to_multi_handle(AICurlEasyRequest_wat& curl_easy_request_w) = 0;
	virtual void finished(AICurlEasyRequest_wat& curl_easy_request_w) = 0;
	virtual void removed_from_multi_handle(AICurlEasyRequest_wat& curl_easy_request_w) = 0;
	// Avoid compiler warning.
	virtual ~AICurlEasyHandleEvents() { }
};

// Pointer to data we're going to POST.
class AIPostField : public LLThreadSafeRefCount {
  protected:
	char const* mData;

  public:
	AIPostField(char const* data) : mData(data) { }
	char const* data(void) const { return mData; }
};

// The pointer to the data that we have to POST is passed around as AIPostFieldPtr,
// which causes it to automatically clean up when there are no pointers left
// pointing to it.
typedef LLPointer<AIPostField> AIPostFieldPtr;

#include "aicurlprivate.h"

// AICurlPrivate::CurlEasyRequestPtr, a boost::intrusive_ptr, is no more threadsafe than a
// builtin type, but wrapping it in AIThreadSafe is obviously not going to help here.
// Therefore we use the following trick: we wrap CurlEasyRequestPtr too, and only allow
// read accesses on it.

// AICurlEasyRequest: a thread safe, reference counting, auto-cleaning curl easy handle.
class AICurlEasyRequest {
  private:
	// Use AICurlEasyRequestStateMachine, not AICurlEasyRequest.
	friend class AICurlEasyRequestStateMachine;

	// Initial construction is allowed (thread-safe).
	// Note: If ThreadSafeCurlEasyRequest() throws then the memory allocated is still freed.
	// 'new' never returned however and neither the constructor nor destructor of mCurlEasyRequest is called in this case.
	// This might throw AICurlNoEasyHandle.
	AICurlEasyRequest(bool buffered) :
	    mCurlEasyRequest(buffered ? new AICurlPrivate::ThreadSafeBufferedCurlEasyRequest : new AICurlPrivate::ThreadSafeCurlEasyRequest) { }

  public:
	// Used for storing this object in a standard container (see MultiHandle::add_easy_request).
	AICurlEasyRequest(AICurlEasyRequest const& orig) : mCurlEasyRequest(orig.mCurlEasyRequest) { }

	// For the rest, only allow read operations.
	AIThreadSafeSimple<AICurlPrivate::CurlEasyRequest>& operator*(void) const { llassert(mCurlEasyRequest.get()); return *mCurlEasyRequest; }
	AIThreadSafeSimple<AICurlPrivate::CurlEasyRequest>* operator->(void) const { llassert(mCurlEasyRequest.get()); return mCurlEasyRequest.get(); }
	AIThreadSafeSimple<AICurlPrivate::CurlEasyRequest>* get(void) const { return mCurlEasyRequest.get(); }

	// Returns true if this object points to the same CurlEasyRequest object.
	bool operator==(AICurlEasyRequest const& cer) const { return mCurlEasyRequest == cer.mCurlEasyRequest; }
	
	// Returns true if this object points to a different CurlEasyRequest object.
	bool operator!=(AICurlEasyRequest const& cer) const { return mCurlEasyRequest != cer.mCurlEasyRequest; }
	
	// Queue this request for insertion in the multi session.
	void addRequest(void);

	// Queue a command to remove this request from the multi session (or cancel a queued command to add it).
	void removeRequest(void);

	// Returns true when this AICurlEasyRequest wraps a AICurlPrivate::ThreadSafeBufferedCurlEasyRequest.
	bool isBuffered(void) const { return mCurlEasyRequest->isBuffered(); }

  private:
	// The actual pointer to the ThreadSafeCurlEasyRequest instance.
	AICurlPrivate::CurlEasyRequestPtr mCurlEasyRequest;

  private:
	// Assignment would not be thread-safe; we may create this object and read from it.
	// Note: Destruction is implicitly assumed thread-safe, as it would be a logic error to
	// destruct it while another thread still needs it, concurrent or not.
	AICurlEasyRequest& operator=(AICurlEasyRequest const&) { return *this; }

  public:
	// Instead of assignment, it might be helpful to use swap.
	void swap(AICurlEasyRequest& cer) { mCurlEasyRequest.swap(cer.mCurlEasyRequest); }

  public:
	// The more exotic member functions of this class, to deal with passing this class
	// as CURLOPT_PRIVATE pointer to a curl handle and afterwards restore it.
	// For "internal use" only; don't use things from AICurlPrivate yourself.

	// It's thread-safe to give read access the underlaying boost::intrusive_ptr.
	// It's not OK to then call get() on that and store the AICurlPrivate::ThreadSafeCurlEasyRequest* separately.
	AICurlPrivate::CurlEasyRequestPtr const& get_ptr(void) const { return mCurlEasyRequest; }

	// If we have a correct (with regard to reference counting) AICurlPrivate::CurlEasyRequestPtr,
	// then it's OK to construct a AICurlEasyRequest from it.
	// Note that the external AICurlPrivate::CurlEasyRequestPtr needs its own locking, because
	// it's not thread-safe in itself.
	AICurlEasyRequest(AICurlPrivate::CurlEasyRequestPtr const& ptr) : mCurlEasyRequest(ptr) { }

	// This one is obviously dangerous. It's for use only in MultiHandle::check_run_count.
	// See also the long comment in CurlEasyRequest::finalizeRequest with regard to CURLOPT_PRIVATE.
	explicit AICurlEasyRequest(AICurlPrivate::ThreadSafeCurlEasyRequest* ptr) : mCurlEasyRequest(ptr) { }
};

// Write Access Type for the buffer.
struct AICurlResponderBuffer_wat : public AIAccess<AICurlPrivate::CurlResponderBuffer> {
  explicit AICurlResponderBuffer_wat(AICurlPrivate::ThreadSafeBufferedCurlEasyRequest& lockobj) :
	  AIAccess<AICurlPrivate::CurlResponderBuffer>(lockobj) { }
  AICurlResponderBuffer_wat(AIThreadSafeSimple<AICurlPrivate::CurlEasyRequest>& lockobj) :
	  AIAccess<AICurlPrivate::CurlResponderBuffer>(static_cast<AICurlPrivate::ThreadSafeBufferedCurlEasyRequest&>(lockobj)) { }
};

#define AICurlPrivate DONTUSE_AICurlPrivate

#endif
