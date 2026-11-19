#ifdef _WIN32
#include <crtdbg.h>
#endif
#include <El/Credits/credits.hpp>
#include "httpRequest.h"
#include <Es/Strings/bString.hpp>
#include <Es/Strings/rString.hpp>
#include <string>

//#define VERIFY_CERTIFICATE

#ifndef _CONSOLE
#include <libcurl/include/curl/curl.h>
#endif

REGISTER_CREDITS("rapidjson","2011 Milo Yip","")
REGISTER_CREDITS("libcurl","Copyright (c) 1996 - 2012, Daniel Stenberg","")

static RString ServerInstanceId;
static RString ServerInstanceKey;

struct CurlInit
{
  CurlInit()
  {
    // CURL_GLOBAL_WIN32 initializes sockets (WSAStartup), this can be done multiple times
    // CURL_GLOBAL_SSL initializes SSL
#ifdef _WIN32
#ifndef _CONSOLE
    curl_global_init(CURL_GLOBAL_WIN32|CURL_GLOBAL_SSL);

	ServerInstanceId = "000000"; // default Instance ID
#endif
#endif
  }
  ~CurlInit()
  {
#ifdef _WIN32
#ifndef _CONSOLE
    curl_global_cleanup();
#endif
#endif
  }
} GCurlInit;

struct write_callbackCtx
{
  RString out;
};

static size_t write_callback(void *buffer, size_t size, size_t nmemb, void *data)
{
  write_callbackCtx *ctx = reinterpret_cast<write_callbackCtx *>(data);
  ctx->out = ctx->out + RString(reinterpret_cast<const char *>(buffer),size*nmemb);
  return size*nmemb;
}


static rapidjson::DocumentValue CreateErrorObject(const char *errorString)
{
  rapidjson::DocumentValue doc;
  // construct a JSON object containing the error message
  doc.AddMemberString("error", errorString);
  return doc;
}


/// after a transport failure, skip further requests for a while so an unreachable hive does not stall the server loop
static DWORD HostUnreachableUntil = 0;
static const DWORD HostUnreachableRetryMs = 60000;

//int GetResponse( RString &response, const char *site, const char *query, const char *postString, const char *adv )
int NOTHROW GetResponse( EHttpRequestType eRequest, RString &response, const char* sServerPath, const char* sArgumentList, const char* sPostData )
{
  BString<1024> url;

  unsigned long int cntStart = GetTickCount();

  // append id as next argument or initial
  if( sArgumentList )
	  url.PrintF("%s/%s&id=%s",sServerPath,sArgumentList,ServerInstanceId.Data());
  else
	  url.PrintF("%s/?id=%s",sServerPath,ServerInstanceId.Data());

  RptF( "XXX GetResponse %s", cc_cast(url) );

  if (HostUnreachableUntil && (int)(HostUnreachableUntil - cntStart) > 0)
  {
    response = "HTTP host unreachable, request skipped";
    return 503;
  }

#ifndef _CONSOLE
  try
  {
    CURL *curl = curl_easy_init();

    char myErrorBuffer[CURL_ERROR_SIZE];
    curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,myErrorBuffer);
#if _DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); 
#endif
    curl_easy_setopt(curl, CURLOPT_URL, url.cstr());


#ifdef VERIFY_CERTIFICATE
/*
curl_easy_setopt(c, CURLOPT_URL, "https://www.site.com");
curl_easy_setopt(c, CURLOPT_CAINFO, "/path/to/the/certificate.crt");
curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1);
*/
#else
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
#endif

	curl_easy_setopt(curl, CURLOPT_TIMEOUT,20);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,5);
    /* no need to use CURLOPT_SSL_VERIFYPEER, CURLOPT_SSL_VERIFYHOST - defaults are good */

    struct curl_slist *slist=NULL;
    if( eRequest == EHTTPREQ_POST )
    {
      curl_easy_setopt(curl, CURLOPT_POST, 1);

	  if( sPostData )
	  {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, sPostData);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(sPostData));

	    slist = curl_slist_append(slist,"Content-type: application/json");

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
	  }
	  else
	  {
		  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
		  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);

		  slist = curl_slist_append(slist,"Content-type: application/json");

		  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
	  }
    }

    
    write_callbackCtx ctx;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (slist) curl_slist_free_all(slist);
    curl_easy_cleanup(curl);

	RptF( "XXX finished in %d result %d", (GetTickCount()-cntStart), http_code );

    // TODO: check response, handle HTTP error
    switch (res)
    {
      case CURLE_OK:
        HostUnreachableUntil = 0;
        if (http_code<400)
        {
          response = ctx.out;
          return http_code;
        }
        switch (http_code)
        {
          case 404: 
            response = Format("HTTP error %d: '%s' not found",http_code,cc_cast(url));
            break;
          default:
            response = Format("HTTP error %d: URL '%s'",http_code,cc_cast(url));
            break;
        }
        break;
      default:
        http_code = 500;
        response = curl_easy_strerror(res);
        HostUnreachableUntil = GetTickCount() + HostUnreachableRetryMs;
        RptF("HTTP request failed (%s), skipping requests for %d s", cc_cast(response), HostUnreachableRetryMs/1000);
        break;
    }
    return http_code;
  }
  catch (...)
  {
    // any failure: return a NULL
    response = "Unhandled exception accessing http";
    return 500;
  }
#else
  // any failure: return a NULL
  response = "Disabled for Binarize";
  return 500;
#endif
}

rapidjson::DocumentValue GetResponseJSON(const char *site, const char *query, rapidjson::DocumentValue &data)
{
  BString<1024> url;
  url.PrintF("%s%s?id=",site,query,ServerInstanceId.Data());

  try
  {
    rapidjson::StringBuffer dataBuffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(dataBuffer);
    data->Accept(writer);
    const char* postString = dataBuffer.GetString();

	BString<1024> url;
	url.PrintF("%s%s",site,query);

    RString responseStr;
    long http_code = GetResponse(EHTTPREQ_POST,responseStr,url,NULL,postString);

    if (http_code<400)
    {
      rapidjson::DocumentValue doc;
      doc->Parse<rapidjson::kParseDefaultFlags>(responseStr);
      return doc;
    }

    return CreateErrorObject(responseStr);
  }
  catch (...)
  {
    // any failure: return a NULL
    return CreateErrorObject("Unhandled exception while registration");
  }
  return rapidjson::DocumentValue();
}


void SetInstanceId( const char* id )
{
	ServerInstanceId = id;
}

void SetInstanceKey( const char* key )
{
	ServerInstanceKey = key;
}

const char* GetInstanceId( void )
{
	return ServerInstanceId.Data();
}

const char* GetInstanceKey( void )
{
	return ServerInstanceKey.Data();
}

