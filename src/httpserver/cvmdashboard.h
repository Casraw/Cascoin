// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_HTTPSERVER_CVMDASHBOARD_H
#define CASCOIN_HTTPSERVER_CVMDASHBOARD_H

#include <string>

class HTTPRequest;

/** Initialize CVM Dashboard HTTP handlers */
void InitCVMDashboardHandlers();

/** Handler for static dashboard files */
bool CVMDashboardRequestHandler(HTTPRequest* req, const std::string& strReq);

/** Get MIME type for file extension */
std::string GetMimeType(const std::string& path);

/** Read file from disk */
std::string ReadDashboardFile(const std::string& filename);

#endif // CASCOIN_HTTPSERVER_CVMDASHBOARD_H

