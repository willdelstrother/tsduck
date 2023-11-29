//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2023, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/license
//
//----------------------------------------------------------------------------
//
//  Perform a simple Web request. Common parts. See specific parts in
//  unix/tsWebRequestGuts.cpp and windows/tsWebRequestGuts.cpp.
//
//----------------------------------------------------------------------------

#include "tsWebRequest.h"
#include "tsFatal.h"
#include "tsFileUtils.h"
#include "tsSysUtils.h"
#include "tsErrCodeReport.h"
#include "tsSingleton.h"
#include "tsURL.h"


//----------------------------------------------------------------------------
// A private singleton to initialize the default proxy from environment
// variables https_proxy and http_proxy.
//----------------------------------------------------------------------------

namespace {
    class DefaultProxy
    {
        TS_DECLARE_SINGLETON(DefaultProxy);
    public:
        const ts::URL url;
    };

    TS_DEFINE_SINGLETON(DefaultProxy);

    DefaultProxy::DefaultProxy() :
        url(ts::GetEnvironment(u"https_proxy", ts::GetEnvironment(u"http_proxy")))
    {
    }
}

// WebRequest static fields:
ts::UString ts::WebRequest::_defaultProxyHost(DefaultProxy::Instance().url.getHost());
uint16_t    ts::WebRequest::_defaultProxyPort = DefaultProxy::Instance().url.getPort();
ts::UString ts::WebRequest::_defaultProxyUser(DefaultProxy::Instance().url.getUserName());
ts::UString ts::WebRequest::_defaultProxyPassword(DefaultProxy::Instance().url.getPassword());
const ts::UString ts::WebRequest::DEFAULT_USER_AGENT(u"tsduck");


//----------------------------------------------------------------------------
// Constructor.
//----------------------------------------------------------------------------

ts::WebRequest::WebRequest(Report& report) :
    _report(report)
{
    allocateGuts();
    CheckNonNull(_guts);
}


//----------------------------------------------------------------------------
// Destructor.
//----------------------------------------------------------------------------

ts::WebRequest::~WebRequest()
{
    if (_guts != nullptr) {
        deleteGuts();
        _guts = nullptr;
    }
    if (_deleteCookiesFile) {
        deleteCookiesFile();
    }
}


//----------------------------------------------------------------------------
// Set/get proxy options.
//----------------------------------------------------------------------------

void ts::WebRequest::setProxyHost(const UString& host, uint16_t port)
{
    _proxyHost = host;
    _proxyPort = port;
}

void ts::WebRequest::setProxyUser(const UString& user, const UString& password)
{
    _proxyUser = user;
    _proxyPassword = password;
}

void ts::WebRequest::SetDefaultProxyHost(const UString& host, uint16_t port)
{
    _defaultProxyHost = host;
    _defaultProxyPort = port;
}

void ts::WebRequest::SetDefaultProxyUser(const UString& user, const UString& password)
{
    _defaultProxyUser = user;
    _defaultProxyPassword = password;
}

const ts::UString& ts::WebRequest::proxyHost() const
{
    return _proxyHost.empty() ? _defaultProxyHost : _proxyHost;
}

uint16_t ts::WebRequest::proxyPort() const
{
    return _proxyPort == 0 ? _defaultProxyPort : _proxyPort;
}

const ts::UString& ts::WebRequest::proxyUser() const
{
    return _proxyUser.empty() ? _defaultProxyUser : _proxyUser;
}

const ts::UString& ts::WebRequest::proxyPassword() const
{
    return _proxyPassword.empty() ? _defaultProxyPassword : _proxyPassword;
}


//----------------------------------------------------------------------------
// Set global cookie management.
//----------------------------------------------------------------------------

void ts::WebRequest::enableCookies(const UString& fileName)
{
    _useCookies = true;
    // Delete previous cookies file.
    if (_deleteCookiesFile) {
        deleteCookiesFile();
    }
    // If the file name is not specified, delete the temporary file in the destructor.
    _deleteCookiesFile = fileName.empty();
    _cookiesFileName = _deleteCookiesFile ? TempFile(u".cookies") : fileName;
}

void ts::WebRequest::disableCookies()
{
    _useCookies = false;
    if (_deleteCookiesFile) {
        deleteCookiesFile();
    }
}

ts::UString ts::WebRequest::getCookiesFileName() const
{
    return _cookiesFileName;
}

bool ts::WebRequest::deleteCookiesFile() const
{
    if (_cookiesFileName.empty() || !fs::exists(_cookiesFileName)) {
        // No cookies file to delete.
        return true;
    }
    else {
        _report.debug(u"deleting cookies file %s", {_cookiesFileName});
        return fs::remove(_cookiesFileName, &ErrCodeReport(_report, u"error deleting", _cookiesFileName));
    }
}


//----------------------------------------------------------------------------
// Set various arguments from command line.
//----------------------------------------------------------------------------

void ts::WebRequest::setArgs(const ts::WebRequestArgs& args)
{
    if (!args.proxyHost.empty()) {
        setProxyHost(args.proxyHost, args.proxyPort);
    }
    if (!args.proxyUser.empty()) {
        setProxyUser(args.proxyUser, args.proxyPassword);
    }
    if (!args.userAgent.empty()) {
        setUserAgent(args.userAgent);
    }
    if (args.connectionTimeout > 0) {
        setConnectionTimeout(args.connectionTimeout);
    }
    if (args.receiveTimeout > 0) {
        setReceiveTimeout(args.receiveTimeout);
    }
    if (args.useCookies) {
        enableCookies(args.cookiesFile);
    }
    if (args.useCompression) {
        enableCompression();
    }
    for (const auto& it : args.headers) {
        setRequestHeader(it.first, it.second);
    }
}


//----------------------------------------------------------------------------
// Set request headers.
//----------------------------------------------------------------------------

void ts::WebRequest::setRequestHeader(const UString& name, const UString& value)
{
    // Check for duplicates on key AND value (multiple headers with the same key are permitted)
    for (const auto& header: _requestHeaders) {
        if (header.first == name && header.second == value) {
            return;
        }
    }
    _requestHeaders.insert(std::make_pair(name, value));
}

void ts::WebRequest::clearRequestHeaders()
{
    _requestHeaders.clear();
}


//----------------------------------------------------------------------------
// Get the value of one or all headers.
//----------------------------------------------------------------------------

void ts::WebRequest::getResponseHeaders(HeadersMap& headers) const
{
    headers = _responseHeaders;
}

ts::UString ts::WebRequest::reponseHeader(const UString& name) const
{
    const auto it = _responseHeaders.find(name);
    return it == _responseHeaders.end() ? UString() : it->second;
}


//----------------------------------------------------------------------------
// Get the MIME type in the response headers.
//----------------------------------------------------------------------------

ts::UString ts::WebRequest::mimeType(bool simple, bool lowercase) const
{
    // Get complete MIME type.
    UString mime(reponseHeader(u"Content-Type"));

    // Get initial type, before ';', in simple form.
    if (simple) {
        const size_t semi = mime.find(u';');
        if (semi != NPOS) {
            mime.erase(semi);
        }
        mime.trim();
    }

    // Force case.
    if (lowercase) {
        mime.convertToLower();
    }

    return mime;
}


//----------------------------------------------------------------------------
// Process a list of headers. Header lines are terminated by LF or CRLF.
//----------------------------------------------------------------------------

void ts::WebRequest::processReponseHeaders(const UString& text)
{
    // Split header lines.
    const UString CR(1, u'\r');
    UStringList lines;
    text.toRemoved(CR).split(lines, u'\n', true, true);

    // Process headers one by one.
    for (const auto& line : lines) {

        _report.debug(u"HTTP header: %s", {line});
        const size_t colon = line.find(u':');
        size_t size = 0;

        if (line.startWith(u"HTTP/")) {
            // This is the initial header. When we receive this, this is either
            // the first time we are called for this request or we have been
            // redirected to another URL. In all cases, reset the context.
            _responseHeaders.clear();
            _headerContentSize = 0;
            _httpStatus = 0;

            // The HTTP status is in the second field, as in "HTTP/1.1 200 OK".
            UStringVector fields;
            line.split(fields, u' ', true, true);
            if (fields.size() < 2 || !fields[1].toInteger(_httpStatus)) {
                _report.warning(u"no HTTP status found in header: %s", {line});
            }
        }
        else if (colon != NPOS) {
            // Found a real header.
            UString name(line, 0, colon);
            UString value(line, colon + 1, line.size() - colon - 1);
            name.trim();
            value.trim();

            // Insert header.
            _responseHeaders.insert(std::make_pair(name, value));

            // Process specific headers.
            if (name.similar(u"Location")) {
                _finalURL = value;
                _report.debug(u"redirected to %s", {_finalURL});
            }
            else if (name.similar(u"Content-length") && value.toInteger(size)) {
                _headerContentSize = size;
            }
        }
    }
}


//----------------------------------------------------------------------------
// Open an URL and start the transfer.
//----------------------------------------------------------------------------

bool ts::WebRequest::open(const UString& url)
{
    if (url.empty()) {
        _report.error(u"no URL specified");
        return false;
    }

    if (_isOpen) {
        _report.error(u"internal error, transfer already started, cannot download %s", {url});
        return false;
    }

    _finalURL = url;
    _originalURL = url;
    _responseHeaders.clear();
    _contentSize = 0;
    _headerContentSize = 0;
    _httpStatus = 0;
    _interrupted = false;

    // System-specific transfer initialization.
    _isOpen = startTransfer();
    return _isOpen;
}


//----------------------------------------------------------------------------
// Download the content of the URL as binary data.
//----------------------------------------------------------------------------

bool ts::WebRequest::downloadBinaryContent(const UString& url, ByteBlock& data, size_t chunkSize)
{
    data.clear();

    // Transfer initialization.
    if (!open(url)) {
        return false;
    }

    // Initialize download buffers.
    size_t receivedSize = 0;
    data.reserve(_headerContentSize);
    data.resize(chunkSize);
    bool success = true;

    for (;;) {
        // Transfer one chunk.
        size_t thisSize = 0;
        success = receive(data.data() + receivedSize, data.size() - receivedSize, thisSize);
        receivedSize += std::min(thisSize, data.size() - receivedSize);

        // Error or end of transfer.
        if (!success || thisSize == 0) {
            break;
        }

        // Enlarge the buffer for next chunk.
        // Don't do that too often in case of very short transfers.
        if (data.size() - receivedSize < chunkSize / 2) {
            data.resize(receivedSize + chunkSize);
        }
    }

    // Resize data buffer to actually transfered size.
    data.resize(receivedSize);
    return close() && success;
}


//----------------------------------------------------------------------------
// Download the content of the URL as text.
//----------------------------------------------------------------------------

bool ts::WebRequest::downloadTextContent(const UString& url, UString& text, size_t chunkSize)
{
    // Download the content as raw binary data.
    ByteBlock data;
    if (downloadBinaryContent(url, data, chunkSize)) {
        // Convert to UTF-8.
        text.assignFromUTF8(reinterpret_cast<const char*>(data.data()), data.size());
        // Remove all CR, just keep the LF.
        text.remove(u'\r');
        return true;
    }
    else {
        // Download error.
        text.clear();
        return false;
    }
}


//----------------------------------------------------------------------------
// Download the content of the URL in a file.
//----------------------------------------------------------------------------

bool ts::WebRequest::downloadFile(const UString& url, const UString& fileName, size_t chunkSize)
{
    // Transfer initialization.
    if (!open(url)) {
        return false;
    }

    // Create the output file.
    std::ofstream file(fileName.toUTF8().c_str(), std::ios::out | std::ios::binary);
    if (!file) {
        _report.error(u"error creating file %s", {fileName});
        close();
        return false;
    }

    std::vector<char> buffer(chunkSize);
    bool success = true;

    for (;;) {
        // Transfer one chunk.
        size_t thisSize = 0;
        success = receive(buffer.data(), buffer.size(), thisSize);

        // Error or end of transfer.
        if (!success || thisSize == 0) {
            break;
        }

        file.write(buffer.data(), thisSize);
        if (!file) {
            _report.error(u"error saving download to %s", {fileName});
            success = false;
            break;
        }
    }

    // Resize data buffer to actually transfered size.
    file.close();
    return close() && success;
}
