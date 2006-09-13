/* $Id: filter_sru_to_z3950.cpp,v 1.3 2006-09-13 21:49:34 marc Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#include "config.hpp"
#include "filter.hpp"
#include "package.hpp"
#include "util.hpp"
#include "filter_sru_to_z3950.hpp"

#include <yaz/zgdu.h>
#include <yaz/srw.h>

#include <boost/thread/mutex.hpp>

#include <iostream>
#include <sstream>
#include <string>


namespace mp = metaproxy_1;
namespace yf = mp::filter;

namespace metaproxy_1 
{

    template<typename T>
    std::string stringify(const T& x)
    {
        std::ostringstream o;
        o << x;
        return o.str();
    }
}


namespace metaproxy_1 {
    namespace filter {
        class SRUtoZ3950::Rep {
            //friend class SRUtoZ3950;
        public:
            void configure(const xmlNode *xmlnode);
            void process(metaproxy_1::Package &package) const;
        private:
            std::string protocol_type(const Z_HTTP_Request &http_req) const;
            std::string debug_http(const Z_HTTP_Request &http_req) const;
            void http_response(metaproxy_1::Package &package, 
                               const std::string &content, 
                               int http_code = 200) const;
        };
    }
}

yf::SRUtoZ3950::SRUtoZ3950() : m_p(new Rep)
{
}

yf::SRUtoZ3950::~SRUtoZ3950()
{  // must have a destructor because of boost::scoped_ptr
}

void yf::SRUtoZ3950::configure(const xmlNode *xmlnode)
{
    m_p->configure(xmlnode);
}

void yf::SRUtoZ3950::process(mp::Package &package) const
{
    m_p->process(package);
}

void yf::SRUtoZ3950::Rep::configure(const xmlNode *xmlnode)
{
}

void yf::SRUtoZ3950::Rep::process(mp::Package &package) const
{
    Z_GDU *zgdu_req = package.request().get();

    // ignoring all non HTTP_Request packages
    if (!zgdu_req || !(zgdu_req->which == Z_GDU_HTTP_Request)){
        package.move();
        return;
    }


 
    
    // only working on  HTTP_Request packages now
    Z_HTTP_Request* http_req =  zgdu_req->u.HTTP_Request;

    // TODO: SRU package checking and translation to Z3950 package
 
    std::cout << protocol_type(*http_req) << "\n";

    package.move();
    return;



//     int ret_code = 2;  /* 2=NOT TAKEN, 1=TAKEN, 0=SOAP TAKEN */
//     Z_SRW_PDU *sru_res = 0;
//     Z_SOAP *soap_package = 0;
//     char *charset = 0;
//     Z_SRW_diagnostic *diagnostic = 0;
//     int num_diagnostic = 0;
//     mp::odr odr;
    
//     ret_code = yaz_sru_decode(http_req, &sru_res, 
//                                    &soap_package, odr, &charset,
//                                    &diagnostic, &num_diagnostic)))
//      
//     ret_code = yaz_srw_decode(http_req, &sru_res, 
//                                         &soap_package, odr, &charset)))

    


    // sending Z3950 package through pipeline
    package.move();


    // TODO: Z3950 response parsing and translation to SRU package
    Z_HTTP_Response* http_res = 0;

    std::string content = debug_http(*http_req);
    int http_code = 200;
    
    http_response(package, content, http_code);

    //package.response() = zgdu_res;
}

std::string 
yf::SRUtoZ3950::Rep::protocol_type(const Z_HTTP_Request &http_req) const
{
    std::string protocol("HTTP");

    const std::string mime_text_xml("text/xml");
    const std::string mime_soap_xml("application/soap+xml");
    const std::string mime_urlencoded("application/x-www-form-urlencoded");

    const std::string http_method(http_req.method);

    //const std::string http_type(z_HTTP_header_lookup(http_req.headers, 
    //                                               "Content-Type"));
    // TODO: there is a sude condition in the above, fix it in YAZ
    const std::string http_type("xyz");

    if (http_method == "GET")
        return "SRU GET";

    if (http_method == "POST"
              && http_type  == mime_urlencoded)
        return "SRU POST";
    
    if ( http_method == "POST"
         && (http_type  == mime_text_xml
             || http_type  == mime_soap_xml))
        return "SRU SOAP";

    return "HTTP";
}

std::string 
yf::SRUtoZ3950::Rep::debug_http(const Z_HTTP_Request &http_req) const
{
    std::string message("<html>\n<body>\n<h1>"
                        "Metaproxy SRUtoZ3950 filter"
                        "</h1>\n");
    
    message += "<h3>HTTP Info</h3><br/>\n";
    message += "<p>\n";
    message += "<b>Method: </b> " + std::string(http_req.method) + "<br/>\n";
    message += "<b>Version:</b> " + std::string(http_req.version) + "<br/>\n";
    message += "<b>Path:   </b> " + std::string(http_req.path) + "<br/>\n";
    message += "<b>Content-Type:</b>"
        + std::string(z_HTTP_header_lookup(http_req.headers, "Content-Type"))
        + "<br/>\n";
    message += "<b>Content-Length:</b>"
        + std::string(z_HTTP_header_lookup(http_req.headers, 
                                           "Content-Length"))
        + "<br/>\n";
    message += "</p>\n";    
    
    message += "<h3>Headers</h3><br/>\n";
    message += "<p>\n";    
    Z_HTTP_Header* header = http_req.headers;
    while (header){
        message += "<b>Header: </b> <i>" 
            + std::string(header->name) + ":</i> "
            + std::string(header->value) + "<br/>\n";
        header = header->next;
    }
    message += "</p>\n";    
    message += "</body>\n</html>\n";
    return message;
}

void yf::SRUtoZ3950::Rep::http_response(metaproxy_1::Package &package, 
                                        const std::string &content, 
                                        int http_code) const
{

    Z_GDU *zgdu_req = package.request().get(); 
    Z_GDU *zgdu_res = 0; 
    mp::odr odr;
    zgdu_res 
       = odr.create_HTTP_Response(package.session(), 
                                  zgdu_req->u.HTTP_Request, 
                                  http_code);
        
    zgdu_res->u.HTTP_Response->content_len = content.size();
    zgdu_res->u.HTTP_Response->content_buf 
        = (char*) odr_malloc(odr, zgdu_res->u.HTTP_Response->content_len);
    
    strncpy(zgdu_res->u.HTTP_Response->content_buf, 
            content.c_str(),  zgdu_res->u.HTTP_Response->content_len);
    
    //z_HTTP_header_add(odr, &hres->headers,
    //                  "Content-Type", content_type.c_str());
    package.response() = zgdu_res;
}




static mp::filter::Base* filter_creator()
{
    return new mp::filter::SRUtoZ3950;
}

extern "C" {
    struct metaproxy_1_filter_struct metaproxy_1_filter_sru_to_z3950 = {
        0,
        "SRUtoZ3950",
        filter_creator
    };
}


/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
