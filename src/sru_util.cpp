/* $Id: sru_util.cpp,v 1.4 2007-01-05 12:26:50 marc Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
*/

#include "sru_util.hpp"
#include "util.hpp"

//#include <yaz/wrbuf.h>
//#include <yaz/querytowrbuf.h>

#include <iostream>
#include <string>

namespace mp = metaproxy_1;

// Doxygen doesn't like mp::gdu, so we use this instead
namespace mp_util = metaproxy_1::util;



bool mp_util::build_sru_debug_package(mp::Package &package)
{
    Z_GDU *zgdu_req = package.request().get();
    if  (zgdu_req && zgdu_req->which == Z_GDU_HTTP_Request)
    {    
        Z_HTTP_Request* http_req =  zgdu_req->u.HTTP_Request;
        std::string content = mp_util::http_headers_debug(*http_req);
        int http_code = 400;    
        mp_util::http_response(package, content, http_code);
        return true;
    }
    package.session().close();
    return false;
}

void mp_util::get_sru_server_info(mp::Package &package, 
                                   Z_SRW_explainRequest 
                                   const *er_req) 
{

    SRUServerInfo sruinfo;

    // getting database info
    std::string database("Default");
    if (er_req && er_req->database)
        database = er_req->database;

    // getting host and port info
    std::string host = package.origin().listen_host();
    std::string port = mp_util::to_string(package.origin().listen_port());

    // overwriting host and port info if set from HTTP Host header
    Z_GDU *zgdu_req = package.request().get();
    if  (zgdu_req && zgdu_req->which == Z_GDU_HTTP_Request)
    {
        Z_HTTP_Request* http_req =  zgdu_req->u.HTTP_Request;
        if (http_req)
        {
            std::string http_host_address
                = mp_util::http_header_value(http_req->headers, "Host");

            std::string::size_type i = http_host_address.rfind(":");
            if (i != std::string::npos)
            {
                host.assign(http_host_address, 0, i);
                port.assign(http_host_address, i + 1, std::string::npos);
            }
        }
    }
}


bool mp_util::build_simple_explain(mp::Package &package, 
                                   mp::odr &odr_en,
                                   Z_SRW_PDU *sru_pdu_res,
                                   Z_SRW_explainRequest 
                                   const *er_req) 
{
    // z3950'fy recordPacking
    int record_packing = Z_SRW_recordPacking_XML;
    if (er_req && er_req->recordPacking && 's' == *(er_req->recordPacking))
        record_packing = Z_SRW_recordPacking_string;

    // getting database info
    std::string database("Default");
    if (er_req && er_req->database)
        database = er_req->database;

    // getting host and port info
    std::string host = package.origin().listen_host();
    std::string port = mp_util::to_string(package.origin().listen_port());

    // overwriting host and port info if set from HTTP Host header
    Z_GDU *zgdu_req = package.request().get();
    if  (zgdu_req && zgdu_req->which == Z_GDU_HTTP_Request)
    {
        Z_HTTP_Request* http_req =  zgdu_req->u.HTTP_Request;
        if (http_req)
        {
            std::string http_host_address
                = mp_util::http_header_value(http_req->headers, "Host");

            std::string::size_type i = http_host_address.rfind(":");
            if (i != std::string::npos)
            {
                host.assign(http_host_address, 0, i);
                port.assign(http_host_address, i + 1, std::string::npos);
            }
        }
    }

    // building SRU explain record
    std::string explain_xml 
        = mp_util::to_string(
            "<explain  xmlns=\"http://explain.z3950.org/dtd/2.0/\">\n"
            "  <serverInfo protocol='SRU'>\n"
            "    <host>")
        + host
        + mp_util::to_string("</host>\n"
            "    <port>")
        + port
        + mp_util::to_string("</port>\n"
            "    <database>")
        + database
        + mp_util::to_string("</database>\n"
            "  </serverInfo>\n"
            "</explain>\n");
    
    
    // preparing explain record insert
    Z_SRW_explainResponse *sru_res = sru_pdu_res->u.explain_response;
    
    // inserting one and only explain record
    
    sru_res->record.recordPosition = odr_intdup(odr_en, 1);
    sru_res->record.recordPacking = record_packing;
    sru_res->record.recordSchema = "http://explain.z3950.org/dtd/2.0/";
    sru_res->record.recordData_len = 1 + explain_xml.size();
    sru_res->record.recordData_buf
        = odr_strdupn(odr_en, (const char *)explain_xml.c_str(), 
                      1 + explain_xml.size());

    return true;
};


bool mp_util::build_sru_response(mp::Package &package, 
                                             mp::odr &odr_en,
                                             Z_SOAP *soap,
                                             const Z_SRW_PDU *sru_pdu_res,
                                             char *charset,
                                             const char *stylesheet) 
{

    // SRU request package translation to Z3950 package
    //if (sru_pdu_res)
    //    std::cout << *(const_cast<Z_SRW_PDU *>(sru_pdu_res)) << "\n";
    //else
    //    std::cout << "SRU empty\n";

    
    Z_GDU *zgdu_req = package.request().get();
    if  (zgdu_req && zgdu_req->which == Z_GDU_HTTP_Request)
    {    
        Z_GDU *zgdu_res //= z_get_HTTP_Response(odr_en, 200);
            = odr_en.create_HTTP_Response(package.session(), 
                                          zgdu_req->u.HTTP_Request, 
                                          200);

        // adding HTTP response code and headers
        Z_HTTP_Response * http_res = zgdu_res->u.HTTP_Response;
        //http_res->code = http_code;
        
        std::string ctype("text/xml");
        if (charset){
            ctype += "; charset=";
            ctype += charset;
        }

        z_HTTP_header_add(odr_en, 
                          &http_res->headers, "Content-Type", ctype.c_str());

         // packaging Z_SOAP into HTML response
         static Z_SOAP_Handler soap_handlers[4] = {
              {"http://www.loc.gov/zing/srw/", 0,
               (Z_SOAP_fun) yaz_srw_codec},
              {"http://www.loc.gov/zing/srw/v1.0/", 0,
               (Z_SOAP_fun) yaz_srw_codec},
              {"http://www.loc.gov/zing/srw/update/", 0,
               (Z_SOAP_fun) yaz_ucp_codec},
              {0, 0, 0}
          };


         // empty stylesheet means NO stylesheet
         if (stylesheet && *stylesheet == '\0')
             stylesheet = 0;
         
         // encoding SRU package
         
         soap->u.generic->p  = (void*) sru_pdu_res;         
         //int ret = 
         z_soap_codec_enc_xsl(odr_en, &soap, 
                              &http_res->content_buf, &http_res->content_len,
                              soap_handlers, charset, stylesheet);
         

         package.response() = zgdu_res;
         return true;
    }
    package.session().close();
    return false;
}



 Z_SRW_PDU * mp_util::decode_sru_request(mp::Package &package,
                                                     mp::odr &odr_de,
                                                     mp::odr &odr_en,
                                                     Z_SRW_PDU *sru_pdu_res,
                                                     Z_SOAP *&soap,
                                                     char *charset,
                                                     char *stylesheet) 
{
    Z_GDU *zgdu_req = package.request().get();
    Z_SRW_PDU *sru_pdu_req = 0;

    //assert((zgdu_req->which == Z_GDU_HTTP_Request));
    
    //ignoring all non HTTP_Request packages
    if (!zgdu_req || !(zgdu_req->which == Z_GDU_HTTP_Request)){
        return 0;
    }
    
    Z_HTTP_Request* http_req =  zgdu_req->u.HTTP_Request;
    if (! http_req)
        return 0;

    // checking if we got a SRU GET/POST/SOAP HTTP package
    // closing connection if we did not ...
    if (0 == yaz_sru_decode(http_req, &sru_pdu_req, &soap, 
                            odr_de, &charset, 
                            &(sru_pdu_res->u.response->diagnostics), 
                            &(sru_pdu_res->u.response->num_diagnostics)))
    {
        if (sru_pdu_res->u.response->num_diagnostics)
        {
            //sru_pdu_res = sru_pdu_res_exp;
            package.session().close();
            return 0;
        }
        return sru_pdu_req;
    }
    else if (0 == yaz_srw_decode(http_req, &sru_pdu_req, &soap, 
                                 odr_de, &charset))
        return sru_pdu_req;
    else 
    {
        //sru_pdu_res = sru_pdu_res_exp;
        package.session().close();
        return 0;
    }
    return 0;
}


bool 
mp_util::check_sru_query_exists(mp::Package &package, 
                                mp::odr &odr_en,
                                Z_SRW_PDU *sru_pdu_res, 
                                Z_SRW_searchRetrieveRequest const *sr_req)
{
    if( (sr_req->query_type == Z_SRW_query_type_cql && !sr_req->query.cql) )
    {
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               7, "query");
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               10, "CQL query is empty");
        return false;
    }
    if( (sr_req->query_type == Z_SRW_query_type_xcql && !sr_req->query.xcql) )
    {
         yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               10, "XCQL query is empty");
         return false;
   }
    if( (sr_req->query_type == Z_SRW_query_type_pqf && !sr_req->query.pqf) )
    {
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               10, "PQF query is empty");
        return false;
    }
    return true;
};




Z_ElementSetNames * 
mp_util::build_esn_from_schema(mp::odr &odr_en, 
                               const char *schema)
{
  if (!schema)
        return 0;
  
    Z_ElementSetNames *esn 
        = (Z_ElementSetNames *) odr_malloc(odr_en, sizeof(Z_ElementSetNames));
    esn->which = Z_ElementSetNames_generic;
    esn->u.generic = odr_strdup(odr_en, schema);
    return esn; 
}


std::ostream& std::operator<<(std::ostream& os, Z_SRW_PDU& srw_pdu) 
{
    os << "SRU";
    
    switch(srw_pdu.which) {
    case  Z_SRW_searchRetrieve_request:
        os << " " << "searchRetrieveRequest";
        {
            Z_SRW_searchRetrieveRequest *sr = srw_pdu.u.request;
            if (sr)
            {
                if (sr->database)
                    os << " " << (sr->database);
                else
                    os << " -";
                if (sr->startRecord)
                    os << " " << *(sr->startRecord);
                else
                    os << " -";
                if (sr->maximumRecords)
                    os << " " << *(sr->maximumRecords);
                else
                    os << " -";
                if (sr->recordPacking)
                    os << " " << (sr->recordPacking);
                else
                    os << " -";

                if (sr->recordSchema)
                    os << " " << (sr->recordSchema);
                else
                    os << " -";
                
                switch (sr->query_type){
                case Z_SRW_query_type_cql:
                    os << " CQL";
                    if (sr->query.cql)
                        os << " " << sr->query.cql;
                    break;
                case Z_SRW_query_type_xcql:
                    os << " XCQL";
                    break;
                case Z_SRW_query_type_pqf:
                    os << " PQF";
                    if (sr->query.pqf)
                        os << " " << sr->query.pqf;
                    break;
                }
            }
        }
        break;
    case  Z_SRW_searchRetrieve_response:
        os << " " << "searchRetrieveResponse";
        {
            Z_SRW_searchRetrieveResponse *sr = srw_pdu.u.response;
            if (sr)
            {
                if (! (sr->num_diagnostics))
                {
                    os << " OK";
                    if (sr->numberOfRecords)
                        os << " " << *(sr->numberOfRecords);
                    else
                        os << " -";
                    //if (sr->num_records)
                    os << " " << (sr->num_records);
                    //else
                    //os << " -";
                    if (sr->nextRecordPosition)
                        os << " " << *(sr->nextRecordPosition);
                    else
                        os << " -";
                } 
                else
                {
                    os << " DIAG";
                    if (sr->diagnostics && sr->diagnostics->uri)
                        os << " " << (sr->diagnostics->uri);
                    else
                        os << " -";
                    if (sr->diagnostics && sr->diagnostics->message)
                        os << " " << (sr->diagnostics->message);
                    else
                        os << " -";
                    if (sr->diagnostics && sr->diagnostics->details)
                        os << " " << (sr->diagnostics->details);
                    else
                        os << " -";
                }
                
                    
            }
        }
        break;
    case  Z_SRW_explain_request:
        os << " " << "explainRequest";
        break;
    case  Z_SRW_explain_response:
        os << " " << "explainResponse";
        break;
    case  Z_SRW_scan_request:
        os << " " << "scanRequest";
        break;
    case  Z_SRW_scan_response:
        os << " " << "scanResponse";
        break;
    case  Z_SRW_update_request:
        os << " " << "updateRequest";
        break;
    case  Z_SRW_update_response:
        os << " " << "updateResponse";
        break;
    default: 
        os << " " << "UNKNOWN";    
    }

    return os;    
}




// mp_util::SRU::SRU_protocol_type
// mp_util::SRU::protocol(const Z_HTTP_Request &http_req) const
// {
//     const std::string mime_urlencoded("application/x-www-form-urlencoded");
//     const std::string mime_text_xml("text/xml");
//     const std::string mime_soap_xml("application/soap+xml");

//     const std::string http_method(http_req.method);
//     const std::string http_type 
//         =  mp_util::http_header_value(http_req.headers, "Content-Type");

//     if (http_method == "GET")
//         return SRU_GET;

//     if (http_method == "POST"
//               && http_type  == mime_urlencoded)
//         return SRU_POST;
    
//     if ( http_method == "POST"
//          && (http_type  == mime_text_xml
//              || http_type  == mime_soap_xml))
//         return SRU_SOAP;

//     return SRU_NONE;
// }

// std::string 
// mp_util::sru_protocol(const Z_HTTP_Request &http_req) const
// {
//     const std::string mime_urlencoded("application/x-www-form-urlencoded");
//     const std::string mime_text_xml("text/xml");
//     const std::string mime_soap_xml("application/soap+xml");

//     const std::string http_method(http_req.method);
//     const std::string http_type 
//         =  mp_util::http_header_value(http_req.headers, "Content-Type");

//     if (http_method == "GET")
//         return "SRU GET";

//     if (http_method == "POST"
//               && http_type  == mime_urlencoded)
//         return "SRU POST";
    
//     if ( http_method == "POST"
//          && (http_type  == mime_text_xml
//              || http_type  == mime_soap_xml))
//         return "SRU SOAP";

//     return "HTTP";
// }

// std::string 
// mp_util::debug_http(const Z_HTTP_Request &http_req) const
// {
//     std::string message("<html>\n<body>\n<h1>"
//                         "Metaproxy SRUtoZ3950 filter"
//                         "</h1>\n");
    
//     message += "<h3>HTTP Info</h3><br/>\n";
//     message += "<p>\n";
//     message += "<b>Method: </b> " + std::string(http_req.method) + "<br/>\n";
//     message += "<b>Version:</b> " + std::string(http_req.version) + "<br/>\n";
//     message += "<b>Path:   </b> " + std::string(http_req.path) + "<br/>\n";

//     message += "<b>Content-Type:</b>"
//         + mp_util::http_header_value(http_req.headers, "Content-Type")
//         + "<br/>\n";
//     message += "<b>Content-Length:</b>"
//         + mp_util::http_header_value(http_req.headers, "Content-Length")
//         + "<br/>\n";
//     message += "</p>\n";    
    
//     message += "<h3>Headers</h3><br/>\n";
//     message += "<p>\n";    
//     Z_HTTP_Header* header = http_req.headers;
//     while (header){
//         message += "<b>Header: </b> <i>" 
//             + std::string(header->name) + ":</i> "
//             + std::string(header->value) + "<br/>\n";
//         header = header->next;
//     }
//     message += "</p>\n";    
//     message += "</body>\n</html>\n";
//     return message;
// }

// void mp_util::http_response(metaproxy_1::Package &package, 
//                                         const std::string &content, 
//                                         int http_code) const
// {

//     Z_GDU *zgdu_req = package.request().get(); 
//     Z_GDU *zgdu_res = 0; 
//     mp::odr odr;
//     zgdu_res 
//        = odr.create_HTTP_Response(package.session(), 
//                                   zgdu_req->u.HTTP_Request, 
//                                   http_code);
        
//     zgdu_res->u.HTTP_Response->content_len = content.size();
//     zgdu_res->u.HTTP_Response->content_buf 
//         = (char*) odr_malloc(odr, zgdu_res->u.HTTP_Response->content_len);
    
//     strncpy(zgdu_res->u.HTTP_Response->content_buf, 
//             content.c_str(),  zgdu_res->u.HTTP_Response->content_len);
    
//     //z_HTTP_header_add(odr, &hres->headers,
//     //                  "Content-Type", content_type.c_str());
//     package.response() = zgdu_res;
// }

/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
