/* $Id: filter_sru_to_z3950.cpp,v 1.22 2006-10-10 14:20:16 marc Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#include "config.hpp"
#include "filter.hpp"
#include "package.hpp"
#include "util.hpp"
#include "gduutil.hpp"
#include "sru_util.hpp"
#include "filter_sru_to_z3950.hpp"

#include <yaz/zgdu.h>
#include <yaz/z-core.h>
#include <yaz/srw.h>
#include <yaz/pquery.h>

#include <boost/thread/mutex.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>

namespace mp = metaproxy_1;
namespace mp_util = metaproxy_1::util;
namespace yf = mp::filter;


namespace metaproxy_1 {
    namespace filter {
        class SRUtoZ3950::Impl {
        public:
            void configure(const xmlNode *xmlnode);
            void process(metaproxy_1::Package &package) const;
        private:
            union SRW_query {char * cql; char * xcql; char * pqf;};
            typedef const int& SRW_query_type;
        private:
            //std::string sru_protocol(const Z_HTTP_Request &http_req) const;
            std::string debug_http(const Z_HTTP_Request &http_req) const;
            //void http_response(mp::Package &package, 
            //                   const std::string &content, 
            //                   int http_code = 200) const;
            bool build_sru_debug_package(mp::Package &package) const;
            bool build_simple_explain(mp::Package &package, 
                                      mp::odr &odr_en,
                                      Z_SRW_PDU *sru_pdu_res,
                                      Z_SRW_explainRequest const *er_req) 
                const;
            bool build_sru_response(mp::Package &package, 
                                    mp::odr &odr_en,
                                    Z_SOAP *soap,
                                    const Z_SRW_PDU *sru_pdu_res,
                                    char *charset,
                                    const char *stylesheet) const;
            Z_SRW_PDU * decode_sru_request(mp::Package &package,   
                                           mp::odr &odr_de,
                                           mp::odr &odr_en,
                                           Z_SRW_PDU *sru_pdu_res,
                                           Z_SOAP *&soap,
                                           char *charset,
                                           char *stylesheet) const;
            bool check_sru_query_exists(mp::Package &package,
                                       mp::odr &odr_en,
                                       Z_SRW_PDU *sru_pdu_res,
                                       Z_SRW_searchRetrieveRequest 
                                       const *sr_req) const;
            bool z3950_build_query(mp::odr &odr_en, Z_Query *z_query, 
                                   const SRW_query &query, 
                                   SRW_query_type query_type) const;
            bool z3950_init_request(mp::Package &package, 
                                         const std::string 
                                         &database = "Default") const;
            bool z3950_close_request(mp::Package &package) const;
            bool z3950_search_request(mp::Package &package,
                                      mp::odr &odr_en,
                                      Z_SRW_PDU *sru_pdu_res,
                                      Z_SRW_searchRetrieveRequest 
                                          const *sr_req) const;
            bool z3950_present_request(mp::Package &package,
                                       mp::odr &odr_en,
                                       Z_SRW_PDU *sru_pdu_res,
                                       Z_SRW_searchRetrieveRequest 
                                       const *sr_req) const;
            bool z3950_scan_request(mp::Package &package,
                                    mp::odr &odr_en,
                                    Z_SRW_PDU *sru_pdu_res,
                                    Z_SRW_scanRequest 
                                    const *sr_req) const;
            Z_ElementSetNames * build_esn_from_schema(mp::odr &odr_en, 
                                                      const char *schema) 
                const;
            int z3950_to_srw_diag(mp::odr &odr_en, 
                                  Z_SRW_searchRetrieveResponse *srw_res,
                                  Z_DefaultDiagFormat *ddf) const;
        };
    }
}

yf::SRUtoZ3950::SRUtoZ3950() : m_p(new Impl)
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

void yf::SRUtoZ3950::Impl::configure(const xmlNode *xmlnode)
{
}

void yf::SRUtoZ3950::Impl::process(mp::Package &package) const
{
    Z_GDU *zgdu_req = package.request().get();

    // ignoring all non HTTP_Request packages
    if (!zgdu_req || !(zgdu_req->which == Z_GDU_HTTP_Request)){
        package.move();
        return;
    }
    
    // only working on  HTTP_Request packages now


    // TODO: Z3950 response parsing and translation to SRU package
    bool ok = true;    

    mp::odr odr_de(ODR_DECODE);
    Z_SRW_PDU *sru_pdu_req = 0;

    mp::odr odr_en(ODR_ENCODE);
    //Z_SRW_PDU *sru_pdu_res = 0;
    Z_SRW_PDU *sru_pdu_res = yaz_srw_get(odr_en, Z_SRW_explain_response);

    Z_SOAP *soap = 0;
    char *charset = 0;
    char *stylesheet = 0;

    if (! (sru_pdu_req = decode_sru_request(package, odr_de, odr_en, 
                                            sru_pdu_res, soap,
                                            charset, stylesheet)))
    {
        build_simple_explain(package, odr_en, sru_pdu_res, 0);
        build_sru_response(package, odr_en, soap, 
                           sru_pdu_res, charset, stylesheet);
        package.session().close();
        return;
    }
    
    
    // SRU request package translation to Z3950 package
    if (sru_pdu_req)
        std::cout << *sru_pdu_req << "\n";
    else
        std::cout << "SRU empty\n";
    

    // explain
    if (sru_pdu_req && sru_pdu_req->which == Z_SRW_explain_request)
    {
        Z_SRW_explainRequest *er_req = sru_pdu_req->u.explain_request;
        //sru_pdu_res = yaz_srw_get(odr_en, Z_SRW_explain_response);

        build_simple_explain(package, odr_en, sru_pdu_res, er_req);
    }

    // searchRetrieve
    else if (sru_pdu_req 
        && sru_pdu_req->which == Z_SRW_searchRetrieve_request
        && sru_pdu_req->u.request)
    {
        Z_SRW_searchRetrieveRequest *sr_req = sru_pdu_req->u.request;   
        
        sru_pdu_res = yaz_srw_get(odr_en, Z_SRW_searchRetrieve_response);

        // checking that we have a query
        ok = check_sru_query_exists(package, odr_en, sru_pdu_res, sr_req);

        if (ok && z3950_init_request(package))
        {
            {
                ok = z3950_search_request(package, odr_en,
                                          sru_pdu_res, sr_req);

                if (ok 
                    && sru_pdu_res->u.response->numberOfRecords
                    && *(sru_pdu_res->u.response->numberOfRecords)
                    && sr_req->maximumRecords
                    && *(sr_req->maximumRecords))
                    
                    ok = z3950_present_request(package, odr_en,
                                               sru_pdu_res,
                                               sr_req);
                z3950_close_request(package);
            }
        }
    }

    // scan
    else if (sru_pdu_req 
             && sru_pdu_req->which == Z_SRW_scan_request
             && sru_pdu_req->u.scan_request)
    {
        Z_SRW_scanRequest  *sr_req = sru_pdu_req->u.scan_request;   

        sru_pdu_res = yaz_srw_get(odr_en, Z_SRW_scan_response);
        
        // we do not do scan at the moment, therefore issuing a diagnostic
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.scan_response->diagnostics), 
                               &(sru_pdu_res->u.scan_response->num_diagnostics), 
                               4, "scan");
 
        // to be used when we do scan
        if (false && z3950_init_request(package))
        {
            z3950_scan_request(package, odr_en, sru_pdu_res, sr_req);    
            z3950_close_request(package);
        }        
    }
    else
    {
        std::cout << "SRU OPERATION NOT SUPPORTED \n";
        sru_pdu_res = yaz_srw_get(odr_en, Z_SRW_explain_response);
        
        // TODO: make nice diagnostic return package 
        package.session().close();
        return;
    }

    //build_sru_debug_package(package);
    build_sru_response(package, odr_en, soap, 
                       sru_pdu_res, charset, stylesheet);
    return;
}


bool yf::SRUtoZ3950::Impl::build_simple_explain(mp::Package &package, 
                                               mp::odr &odr_en,
                                               Z_SRW_PDU *sru_pdu_res,
                                               Z_SRW_explainRequest 
                                               const *er_req) const
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
            "<explain>\n"
            "  <serverInfo protocol='SRU'>\n"
            "  <host>")
        + host
        + mp_util::to_string("</host>\n"
            "  <port>")
        + port
        + mp_util::to_string("</port>\n"
            "  <database>")
        + database
        + mp_util::to_string("</database>\n"
            "  </serverInfo>\n"
            "</explain>\n");
    
    
    // preparing explain record insert
    Z_SRW_explainResponse *sru_res = sru_pdu_res->u.explain_response;
    //sru_res->record 
    //    = (Z_SRW_record *) odr_malloc(odr_en, sizeof(Z_SRW_record));
    
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


bool yf::SRUtoZ3950::Impl::build_sru_debug_package(mp::Package &package) const
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


bool yf::SRUtoZ3950::Impl::build_sru_response(mp::Package &package, 
                                             mp::odr &odr_en,
                                             Z_SOAP *soap,
                                             const Z_SRW_PDU *sru_pdu_res,
                                             char *charset,
                                             const char *stylesheet) 
    const
{

    // SRU request package translation to Z3950 package
    if (sru_pdu_res)
        std::cout << *(const_cast<Z_SRW_PDU *>(sru_pdu_res)) << "\n";
    else
        std::cout << "SRU empty\n";

    
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



 Z_SRW_PDU * yf::SRUtoZ3950::Impl::decode_sru_request(mp::Package &package,
                                                     mp::odr &odr_de,
                                                     mp::odr &odr_en,
                                                     Z_SRW_PDU *sru_pdu_res,
                                                     Z_SOAP *&soap,
                                                     char *charset,
                                                     char *stylesheet) 
     const
{
    Z_GDU *zgdu_req = package.request().get();
    Z_SRW_PDU *sru_pdu_req = 0;

    assert((zgdu_req->which == Z_GDU_HTTP_Request));
    
    //ignoring all non HTTP_Request packages
    //if (!zgdu_req || !(zgdu_req->which == Z_GDU_HTTP_Request)){
    //    return 0;
    //}
    
    Z_HTTP_Request* http_req =  zgdu_req->u.HTTP_Request;
    if (! http_req)
        return 0;

    //Z_SRW_PDU *sru_pdu_res_exp = yaz_srw_get(odr_en, Z_SRW_explain_response);
    
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
yf::SRUtoZ3950::Impl::check_sru_query_exists(mp::Package &package, 
                                            mp::odr &odr_en,
                                            Z_SRW_PDU *sru_pdu_res, 
                                            Z_SRW_searchRetrieveRequest 
                                                              const *sr_req)
    const
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



bool 
yf::SRUtoZ3950::Impl::z3950_init_request(mp::Package &package, 
                                             const std::string &database) const
{
    // prepare Z3950 package
    //Session s;
    //Package z3950_package(s, package.origin());
    Package z3950_package(package.session(), package.origin());
    z3950_package.copy_filter(package);

    // set initRequest APDU
    mp::odr odr_en(ODR_ENCODE);
    Z_APDU *apdu = zget_APDU(odr_en, Z_APDU_initRequest);
    Z_InitRequest *init_req = apdu->u.initRequest;
    //TODO: add user name in apdu
    //TODO: add user passwd in apdu
    //init_req->idAuthentication = org_init->idAuthentication;
    //init_req->implementationId = "IDxyz";
    //init_req->implementationName = "NAMExyz";
    //init_req->implementationVersion = "VERSIONxyz";

    ODR_MASK_SET(init_req->options, Z_Options_search);
    ODR_MASK_SET(init_req->options, Z_Options_present);
    ODR_MASK_SET(init_req->options, Z_Options_namedResultSets);
    ODR_MASK_SET(init_req->options, Z_Options_scan);

    ODR_MASK_SET(init_req->protocolVersion, Z_ProtocolVersion_1);
    ODR_MASK_SET(init_req->protocolVersion, Z_ProtocolVersion_2);
    ODR_MASK_SET(init_req->protocolVersion, Z_ProtocolVersion_3);

    z3950_package.request() = apdu;

    // send Z3950 package
    // std::cout << "z3950_init_request " << *apdu <<"\n";
    z3950_package.move();

    // dead Z3950 backend detection
    if (z3950_package.session().is_closed()){
        package.session().close();
        return false;
    }

    // check successful initResponse
    Z_GDU *z3950_gdu = z3950_package.response().get();

    if (z3950_gdu && z3950_gdu->which == Z_GDU_Z3950 
        && z3950_gdu->u.z3950->which == Z_APDU_initResponse)
         return true;
 
    return false;
}

bool 
yf::SRUtoZ3950::Impl::z3950_close_request(mp::Package &package) const
{
    // close SRU package
    package.session().close();

    // prepare and close Z3950 package 
    Package z3950_package(package.session(), package.origin());
    z3950_package.copy_filter(package);
    z3950_package.session().close();

    // set close APDU
    //mp::odr odr_en(ODR_ENCODE);
    //Z_APDU *apdu = zget_APDU(odr_en, Z_APDU_close);
    //z3950_package.request() = apdu;

    z3950_package.move();

    // check successful close response
    //Z_GDU *z3950_gdu = z3950_package.response().get();
    //if (z3950_gdu && z3950_gdu->which == Z_GDU_Z3950 
    //    && z3950_gdu->u.z3950->which == Z_APDU_close)
    //    return true;

    if (z3950_package.session().is_closed()){
        return true;
    }
    return false;
}

bool 
yf::SRUtoZ3950::Impl::z3950_search_request(mp::Package &package,  
                                          mp::odr &odr_en,
                                          Z_SRW_PDU *sru_pdu_res,
                                          Z_SRW_searchRetrieveRequest 
                                          const *sr_req) const
{

    assert(sru_pdu_res->u.response);

    Package z3950_package(package.session(), package.origin());
    z3950_package.copy_filter(package);

    //add stuff in z3950 apdu
    Z_APDU *apdu = zget_APDU(odr_en, Z_APDU_searchRequest);
    Z_SearchRequest *z_searchRequest = apdu->u.searchRequest;

    // z3950'fy database
    z_searchRequest->num_databaseNames = 1;
    z_searchRequest->databaseNames = (char**)
        odr_malloc(odr_en, sizeof(char *));

    if (sr_req->database)
        z_searchRequest->databaseNames[0] 
            = odr_strdup(odr_en, const_cast<char *>(sr_req->database));
    else
        z_searchRequest->databaseNames[0] 
            = odr_strdup(odr_en, "Default");


    // z3950'fy query
    Z_Query *z_query = (Z_Query *) odr_malloc(odr_en, sizeof(Z_Query));
    z_searchRequest->query = z_query;
 
    if (!z3950_build_query(odr_en, z_query, 
                           (const SRW_query&)sr_req->query, 
                           sr_req->query_type))
    {    
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               7, "query");
        return false;
    }

    z3950_package.request() = apdu;
    //std::cout << "z3950_search_request " << *apdu << "\n";
        
    z3950_package.move();


    Z_GDU *z3950_gdu = z3950_package.response().get();
    //std::cout << "z3950_search_request " << *z3950_gdu << "\n";

    //TODO: check success condition

    //int yaz_diag_bib1_to_srw (int bib1_code);
    //int yaz_diag_srw_to_bib1(int srw_code);
    //Se kode i src/seshigh.c (srw_bend_search, srw_bend_init).

    if (z3950_gdu && z3950_gdu->which == Z_GDU_Z3950 
        && z3950_gdu->u.z3950->which == Z_APDU_searchResponse
        && z3950_gdu->u.z3950->u.searchResponse->searchStatus)
    {

        Z_SearchResponse *sr = z3950_gdu->u.z3950->u.searchResponse;
        if (sr)
        {
            // srw'fy number of records
            sru_pdu_res->u.response->numberOfRecords 
                = (int *) odr_malloc(odr_en, sizeof(int *));
            *(sru_pdu_res->u.response->numberOfRecords) = *(sr->resultCount);

            // srw'fy nextRecordPosition
            //sru_pdu_res->u.response->nextRecordPosition 
            //    = (int *) odr_malloc(odr_en, sizeof(int *));
            //*(sru_pdu_res->u.response->nextRecordPosition) = 1;

        }

        return true;
    }
    
    return false;
}

bool 
yf::SRUtoZ3950::Impl::z3950_present_request(mp::Package &package, 
                                           mp::odr &odr_en,
                                           Z_SRW_PDU *sru_pdu_res,
                                           Z_SRW_searchRetrieveRequest 
                                           const *sr_req)
    const
{
    assert(sru_pdu_res->u.response);

    if (!sr_req)
        return false;

    
    // no need to work if nobody wants record ..
    if (!(sr_req->maximumRecords) || 0 == *(sr_req->maximumRecords))
        return true;

    bool send_z3950_present = true;

    // recordXPath unsupported.
    if (sr_req->recordXPath)
    {
        send_z3950_present = false;
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               72, 0);
    }
    
    // resultSetTTL unsupported.
    // resultSetIdleTime in response
    if (sr_req->resultSetTTL)
    {
        send_z3950_present = false;
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               50, 0);
    }
    
    // sort unsupported
    if (sr_req->sort_type != Z_SRW_sort_type_none)
    {
        send_z3950_present = false;
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               80, 0);
    }
    
    // start record requested negative, or larger than number of records
    if (sr_req->startRecord 
        && 
        ((*(sr_req->startRecord) < 0)       // negative
         ||
         (sru_pdu_res->u.response->numberOfRecords  //out of range
          && *(sr_req->startRecord) 
          > *(sru_pdu_res->u.response->numberOfRecords))
        ))
    {
        send_z3950_present = false;
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               61, 0);
    }    

    // maximumRecords requested negative
    if (sr_req->maximumRecords
        && *(sr_req->maximumRecords) < 0) 
          
    {
        send_z3950_present = false;
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               6, "maximumRecords");
    }    

    // exit on all these above diagnostics
    if (!send_z3950_present)
        return false;

    // now packaging the z3950 present request
    Package z3950_package(package.session(), package.origin());
    z3950_package.copy_filter(package); 
    Z_APDU *apdu = zget_APDU(odr_en, Z_APDU_presentRequest);

    assert(apdu->u.presentRequest);

    // z3950'fy start record position
    if (sr_req->startRecord)
        *(apdu->u.presentRequest->resultSetStartPoint) 
            = *(sr_req->startRecord);
    else 
        *(apdu->u.presentRequest->resultSetStartPoint) = 1;
    
    // z3950'fy number of records requested 
    // protect against requesting records out of range
    if (sr_req->maximumRecords)
        *(apdu->u.presentRequest->numberOfRecordsRequested) 
            = std::min(*(sr_req->maximumRecords), 
                  *(sru_pdu_res->u.response->numberOfRecords)
                  - *(apdu->u.presentRequest->resultSetStartPoint)
                  + 1);
     
    // z3950'fy recordPacking
    int record_packing = Z_SRW_recordPacking_XML;
    if (sr_req->recordPacking && 's' == *(sr_req->recordPacking))
        record_packing = Z_SRW_recordPacking_string;

    // RecordSyntax will always be XML
    (apdu->u.presentRequest->preferredRecordSyntax)
        = yaz_oidval_to_z3950oid (odr_en, CLASS_RECSYN, VAL_TEXT_XML);

    // z3950'fy record schema
     if (sr_req->recordSchema)
     {
         apdu->u.presentRequest->recordComposition 
             = (Z_RecordComposition *) 
               odr_malloc(odr_en, sizeof(Z_RecordComposition));
         apdu->u.presentRequest->recordComposition->which 
             = Z_RecordComp_simple;
         apdu->u.presentRequest->recordComposition->u.simple 
             = build_esn_from_schema(odr_en, 
                                     (const char *) sr_req->recordSchema); 
     }

    // z3950'fy time to live - flagged as diagnostics above
    //if (sr_req->resultSetTTL)

    // attaching Z3950 package to filter chain
    z3950_package.request() = apdu;

    //std::cout << "z3950_present_request " << *apdu << "\n";   
    z3950_package.move();

    //check successful Z3950 present response
    Z_GDU *z3950_gdu = z3950_package.response().get();
    if (!z3950_gdu || z3950_gdu->which != Z_GDU_Z3950 
        || z3950_gdu->u.z3950->which != Z_APDU_presentResponse
        || !z3950_gdu->u.z3950->u.presentResponse)

    {
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics), 
                               &(sru_pdu_res->u.response->num_diagnostics), 
                               2, 0);
        package.session().close();
        return false;
    }
    

    // everything fine, continuing
    //std::cout << "z3950_present_request OK\n";

    Z_PresentResponse *pr = z3950_gdu->u.z3950->u.presentResponse;
    Z_SRW_searchRetrieveResponse *sru_res = sru_pdu_res->u.response;
        
    // checking non surrogate dioagnostics in Z3950 present response package
    if (pr->records 
        && pr->records->which == Z_Records_NSD
        && pr->records->u.nonSurrogateDiagnostic)
    {
        //int http_code =
        z3950_to_srw_diag(odr_en, sru_res, 
                          pr->records->u.nonSurrogateDiagnostic);
        return false;
    }
    
    // copy all records if existing
    if (pr->records && pr->records->which == Z_Records_DBOSD)
    {
        // srw'fy number of returned records
        sru_res->num_records
            = pr->records->u.databaseOrSurDiagnostics->num_records;
        
        sru_res->records 
            = (Z_SRW_record *) odr_malloc(odr_en, 
                                          sru_res->num_records 
                                             * sizeof(Z_SRW_record));
        

        // srw'fy nextRecordPosition
        // next position never zero or behind the last z3950 record 
        if (pr->nextResultSetPosition
            && *(pr->nextResultSetPosition) > 0 
            && *(pr->nextResultSetPosition) 
               <= *(sru_pdu_res->u.response->numberOfRecords))
            sru_res->nextRecordPosition 
                = odr_intdup(odr_en, *(pr->nextResultSetPosition));
        
        // inserting all records
        for (int i = 0; i < sru_res->num_records; i++)
        {
            Z_NamePlusRecord *npr 
                = pr->records->u.databaseOrSurDiagnostics->records[i];
            
            sru_res->records[i].recordPosition 
                = odr_intdup(odr_en, 
                             i + *(apdu->u.presentRequest->resultSetStartPoint));
            
            sru_res->records[i].recordPacking = record_packing;
            
            if (npr->which != Z_NamePlusRecord_databaseRecord)
            {
                sru_res->records[i].recordSchema = "diagnostic";
                sru_res->records[i].recordData_buf = "67";
                sru_res->records[i].recordData_len = 2;
            }
            else
            {
                Z_External *r = npr->u.databaseRecord;
                oident *ent = oid_getentbyoid(r->direct_reference);
                if (r->which == Z_External_octet 
                    && ent->value == VAL_TEXT_XML)
                {
                    sru_res->records[i].recordSchema = "dc";
                    sru_res->records[i].recordData_buf
                        = odr_strdupn(odr_en, 
                                      (const char *)r->u.octet_aligned->buf, 
                                      r->u.octet_aligned->len);
                    sru_res->records[i].recordData_len 
                        = r->u.octet_aligned->len;
                }
                else
                {
                    sru_res->records[i].recordSchema = "diagnostic";
                    sru_res->records[i].recordData_buf = "67";
                    sru_res->records[i].recordData_len = 2;
                }
            }   
        }    
    }
    
    return true;
}

bool 
yf::SRUtoZ3950::Impl::z3950_scan_request(mp::Package &package,
                                        mp::odr &odr_en,
                                        Z_SRW_PDU *sru_pdu_res,
                                        Z_SRW_scanRequest const *sr_req) const 
{
    assert(sru_pdu_res->u.scan_response);

    Package z3950_package(package.session(), package.origin());
    z3950_package.copy_filter(package); 
    //mp::odr odr_en(ODR_ENCODE);
    Z_APDU *apdu = zget_APDU(odr_en, Z_APDU_scanRequest);

    //TODO: add stuff in apdu
    Z_ScanRequest *z_scanRequest = apdu->u.scanRequest;

    // database repackaging
    z_scanRequest->num_databaseNames = 1;
    z_scanRequest->databaseNames = (char**)
        odr_malloc(odr_en, sizeof(char *));
    if (sr_req->database)
        z_scanRequest->databaseNames[0] 
            = odr_strdup(odr_en, const_cast<char *>(sr_req->database));
    else
        z_scanRequest->databaseNames[0] 
            = odr_strdup(odr_en, "Default");


    // query repackaging
    // CQL or XCQL scan is not possible in Z3950, flagging a diagnostic
    if (sr_req->query_type != Z_SRW_query_type_pqf)
    {        
        //send_to_srw_client_error(7, "query");
        return false;
    }

    // PQF query repackaging
    // need to use Z_AttributesPlusTerm structure, not Z_Query
    // this can be digget out of a 
    // Z_query->type1(Z_RPNQuery)->RPNStructure(Z_RPNStructure)
    //   ->u.simple(Z_Operand)->u.attributesPlusTerm(Z_AttributesPlusTerm )

    //Z_Query *z_query = (Z_Query *) odr_malloc(odr_en, sizeof(Z_Query));
    //z_searchRequest->query = z_query;

    //if (!z3950_build_query(odr_en, z_query, 
    //                       (const SRW_query&)sr_req->query, 
    //                       sr_req->query_type))
    //{    
        //send_to_srw_client_error(7, "query");
    //    return false;
    //}

    // TODO: 

    z3950_package.request() = apdu;
    std::cout << "z3950_scan_request " << *apdu << "\n";   

    z3950_package.move();
    //TODO: check success condition
    return true;
    return false;
}

bool yf::SRUtoZ3950::Impl::z3950_build_query(mp::odr &odr_en, Z_Query *z_query, 
                                            const SRW_query &query, 
                                            SRW_query_type query_type) const
{        
    if (query_type == Z_SRW_query_type_cql)
    {
        Z_External *ext = (Z_External *) 
            odr_malloc(odr_en, sizeof(*ext));
        ext->direct_reference = 
            odr_getoidbystr(odr_en, "1.2.840.10003.16.2");
        ext->indirect_reference = 0;
        ext->descriptor = 0;
        ext->which = Z_External_CQL;
        ext->u.cql = const_cast<char *>(query.cql);
        
        z_query->which = Z_Query_type_104;
        z_query->u.type_104 =  ext;
        return true;
    }

    if (query_type == Z_SRW_query_type_pqf)
    {
        Z_RPNQuery *RPNquery;
        YAZ_PQF_Parser pqf_parser;
        
        pqf_parser = yaz_pqf_create ();
        
        RPNquery = yaz_pqf_parse (pqf_parser, odr_en, query.pqf);
        if (!RPNquery)
        {
            std::cout << "TODO: Handeling of bad PQF\n";
            std::cout << "TODO: Diagnostic to be send\n";
        }
        z_query->which = Z_Query_type_1;
        z_query->u.type_1 =  RPNquery;
        
        yaz_pqf_destroy(pqf_parser);
        return true;
    }
    return false;
}


// std::string 
// yf::SRUtoZ3950::Impl::sru_protocol(const Z_HTTP_Request &http_req) const
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
// yf::SRUtoZ3950::Impl::debug_http(const Z_HTTP_Request &http_req) const
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

// void yf::SRUtoZ3950::Impl::http_response(metaproxy_1::Package &package, 
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


Z_ElementSetNames * 
yf::SRUtoZ3950::Impl::build_esn_from_schema(mp::odr &odr_en, 
                                           const char *schema) const
{
  if (!schema)
        return 0;
  
    Z_ElementSetNames *esn 
        = (Z_ElementSetNames *) odr_malloc(odr_en, sizeof(Z_ElementSetNames));
    esn->which = Z_ElementSetNames_generic;
    esn->u.generic = odr_strdup(odr_en, schema);
    return esn; 
}

int 
yf::SRUtoZ3950::Impl::z3950_to_srw_diag(mp::odr &odr_en, 
                                       Z_SRW_searchRetrieveResponse *sru_res,
                                       Z_DefaultDiagFormat *ddf) const
{
    int bib1_code = *ddf->condition;
    if (bib1_code == 109)
        return 404;
    sru_res->num_diagnostics = 1;
    sru_res->diagnostics = (Z_SRW_diagnostic *)
        odr_malloc(odr_en, sizeof(*sru_res->diagnostics));
    yaz_mk_std_diagnostic(odr_en, sru_res->diagnostics,
                          yaz_diag_bib1_to_srw(*ddf->condition), 
                          ddf->u.v2Addinfo);
    return 0;
}



static mp::filter::Base* filter_creator()
{
    return new mp::filter::SRUtoZ3950;
}

extern "C" {
    struct metaproxy_1_filter_struct metaproxy_1_filter_sru_to_z3950 = {
        0,
        "sru_z3950",
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
