/* $Id: filter_sru_to_z3950.cpp,v 1.28 2007-01-25 13:52:56 adam Exp $
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
#include <map>

namespace mp = metaproxy_1;
namespace mp_util = metaproxy_1::util;
namespace yf = mp::filter;


namespace metaproxy_1 {
    namespace filter {
        class SRUtoZ3950::Impl {
        public:
            void configure(const xmlNode *xmlnode);
            void process(metaproxy_1::Package &package);
        private:
            union SRW_query {char * cql; char * xcql; char * pqf;};
            typedef const int& SRW_query_type;
            std::map<std::string, const xmlNode *> m_database_explain;
        private:

            bool z3950_build_query(mp::odr &odr_en, Z_Query *z_query, 
                                   const SRW_query &query, 
                                   SRW_query_type query_type) const;

            bool z3950_init_request(mp::Package &package, 
                                    mp::odr &odr_en,
                                    Z_SRW_PDU *sru_pdu_res,
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

            bool z3950_to_srw_diagnostics_ok(mp::odr &odr_en, 
                                  Z_SRW_searchRetrieveResponse *srw_res,
                                  Z_Records *records) const;

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

void yf::SRUtoZ3950::Impl::configure(const xmlNode *confignode)
{
    const xmlNode * dbnode;
    
    for (dbnode = confignode->children; dbnode; dbnode = dbnode->next){
        if (dbnode->type != XML_ELEMENT_NODE)
            continue;
        
        std::string database;
        mp::xml::check_element_mp(dbnode, "database");

        for (struct _xmlAttr *attr = dbnode->properties; 
             attr; attr = attr->next){
            
            mp::xml::check_attribute(attr, "", "name");
            database = mp::xml::get_text(attr);
             
            const xmlNode *explainnode;
            for (explainnode = dbnode->children; 
                 explainnode; explainnode = explainnode->next){
                if (explainnode->type != XML_ELEMENT_NODE)
                    continue;
                if (explainnode)
                    break;
            }
            // assigning explain node to database name - no check yet 
            m_database_explain.insert(std::make_pair(database, explainnode));
         }
    }
}

void yf::SRUtoZ3950::Impl::process(mp::Package &package)
{
    Z_GDU *zgdu_req = package.request().get();

    // ignoring all non HTTP_Request packages
    if (!zgdu_req || !(zgdu_req->which == Z_GDU_HTTP_Request)){
        package.move();
        return;
    }
    
    // only working on  HTTP_Request packages now

    bool ok = true;    

    mp::odr odr_de(ODR_DECODE);
    Z_SRW_PDU *sru_pdu_req = 0;

    mp::odr odr_en(ODR_ENCODE);
    Z_SRW_PDU *sru_pdu_res = yaz_srw_get(odr_en, Z_SRW_explain_response);

    // determine database with the HTTP header information only
    mp_util::SRUServerInfo sruinfo = mp_util::get_sru_server_info(package);
    std::map<std::string, const xmlNode *>::iterator idbexp;
    idbexp = m_database_explain.find(sruinfo.database);

    // assign explain config XML DOM node if database is known
    const xmlNode *explainnode = 0;
    if (idbexp != m_database_explain.end()){
        explainnode = idbexp->second;
    }
    // just moving package if database is not known
    else {
        package.move();
        return;
    }
    

    // decode SRU request
    Z_SOAP *soap = 0;
    char *charset = 0;
    char *stylesheet = 0;

    // filter acts as sink for non-valid SRU requests
    if (! (sru_pdu_req = mp_util::decode_sru_request(package, odr_de, odr_en, 
                                            sru_pdu_res, soap,
                                            charset, stylesheet)))
    {
        mp_util::build_sru_explain(package, odr_en, sru_pdu_res, 
                                   sruinfo, explainnode);
        mp_util::build_sru_response(package, odr_en, soap, 
                           sru_pdu_res, charset, stylesheet);
        package.session().close();
        return;
    }
    
    // filter acts as sink for SRU explain requests
    if (sru_pdu_req && sru_pdu_req->which == Z_SRW_explain_request)
    {
        Z_SRW_explainRequest *er_req = sru_pdu_req->u.explain_request;
        //mp_util::build_simple_explain(package, odr_en, sru_pdu_res, 
        //                           sruinfo, er_req);
        mp_util::build_sru_explain(package, odr_en, sru_pdu_res, 
                                   sruinfo, explainnode, er_req);
        mp_util::build_sru_response(package, odr_en, soap, 
                                    sru_pdu_res, charset, stylesheet);
        return;
    }

    // searchRetrieve
    else if (sru_pdu_req 
        && sru_pdu_req->which == Z_SRW_searchRetrieve_request
        && sru_pdu_req->u.request)
    {
        Z_SRW_searchRetrieveRequest *sr_req = sru_pdu_req->u.request;   
        
        sru_pdu_res = yaz_srw_get(odr_en, Z_SRW_searchRetrieve_response);

        // checking that we have a query
        ok = mp_util::check_sru_query_exists(package, odr_en, 
                                             sru_pdu_res, sr_req);

        if (ok && z3950_init_request(package, odr_en, sru_pdu_res))
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
        if (false && z3950_init_request(package, odr_en, sru_pdu_res))
        {
            z3950_scan_request(package, odr_en, sru_pdu_res, sr_req);    
            z3950_close_request(package);
        }        
    }
    else
    {
        //std::cout << "SRU OPERATION NOT SUPPORTED \n";
        sru_pdu_res = yaz_srw_get(odr_en, Z_SRW_explain_response);
        
        // TODO: make nice diagnostic return package 
        package.session().close();
        return;
    }

    // build and send SRU response
    mp_util::build_sru_response(package, odr_en, soap, 
                                sru_pdu_res, charset, stylesheet);
    return;
}



bool 
yf::SRUtoZ3950::Impl::z3950_init_request(mp::Package &package, 
                                         mp::odr &odr_en,
                                         Z_SRW_PDU *sru_pdu_res,
                                         const std::string &database) const
{
    // prepare Z3950 package
    Package z3950_package(package.session(), package.origin());
    z3950_package.copy_filter(package);

    // set initRequest APDU
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
    z3950_package.move();

    // dead Z3950 backend detection
    if (z3950_package.session().is_closed()){
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics),
                               &(sru_pdu_res->u.response->num_diagnostics),
                               2, 0);
        package.session().close();
        return false;
    }

    // check successful initResponse
    Z_GDU *z3950_gdu = z3950_package.response().get();

    if (z3950_gdu && z3950_gdu->which == Z_GDU_Z3950 
        && z3950_gdu->u.z3950->which == Z_APDU_initResponse 
        && *z3950_gdu->u.z3950->u.initResponse->result)
         return true;
 
    yaz_add_srw_diagnostic(odr_en,
                           &(sru_pdu_res->u.response->diagnostics),
                           &(sru_pdu_res->u.response->num_diagnostics),
                           2, 0);
    package.session().close();
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
        
    // send Z39.50 package off to backend
    z3950_package.move();


    Z_GDU *z3950_gdu = z3950_package.response().get();

    //TODO: check success condition
    //int yaz_diag_bib1_to_srw (int bib1_code);
    //int yaz_diag_srw_to_bib1(int srw_code);
    //Se kode i src/seshigh.c (srw_bend_search, srw_bend_init).

    if (!z3950_gdu || z3950_gdu->which != Z_GDU_Z3950 
        || z3950_gdu->u.z3950->which != Z_APDU_searchResponse
        || !z3950_gdu->u.z3950->u.searchResponse
        || !z3950_gdu->u.z3950->u.searchResponse->searchStatus)
    {
        yaz_add_srw_diagnostic(odr_en,
                               &(sru_pdu_res->u.response->diagnostics),
                               &(sru_pdu_res->u.response->num_diagnostics),
                               2, 0);
        package.session().close();
        return false;
    }
    
    // everything fine, continuing
    Z_SearchResponse *sr = z3950_gdu->u.z3950->u.searchResponse;

    // checking non surrogate diagnostics in Z3950 search response package
    if (!z3950_to_srw_diagnostics_ok(odr_en, sru_pdu_res->u.response, 
                                     sr->records))
    {
        return false;
    }

    // Finally, roll on and srw'fy number of records
    sru_pdu_res->u.response->numberOfRecords 
        = (int *) odr_malloc(odr_en, sizeof(int *));
    *(sru_pdu_res->u.response->numberOfRecords) = *(sr->resultCount);
    
    // srw'fy nextRecordPosition
    //sru_pdu_res->u.response->nextRecordPosition 
    //    = (int *) odr_malloc(odr_en, sizeof(int *));
    //*(sru_pdu_res->u.response->nextRecordPosition) = 1;

    return true;
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
             = mp_util::build_esn_from_schema(odr_en,
                                      (const char *) sr_req->recordSchema); 
     }

    // z3950'fy time to live - flagged as diagnostics above
    //if (sr_req->resultSetTTL)

    // attaching Z3950 package to filter chain
    z3950_package.request() = apdu;

    // sending Z30.50 present request 
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

    Z_PresentResponse *pr = z3950_gdu->u.z3950->u.presentResponse;
    Z_SRW_searchRetrieveResponse *sru_res = sru_pdu_res->u.response;
        

    // checking non surrogate diagnostics in Z3950 present response package
    if (!z3950_to_srw_diagnostics_ok(odr_en, sru_pdu_res->u.response, 
                                     pr->records))
        return false;
    

    
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


bool 
yf::SRUtoZ3950::Impl::z3950_to_srw_diagnostics_ok(mp::odr &odr_en, 
                                                  Z_SRW_searchRetrieveResponse 
                                                  *sru_res,
                                                  Z_Records *records) const
{
    // checking non surrogate diagnostics in Z3950 present response package
    if (records 
        && records->which == Z_Records_NSD
        && records->u.nonSurrogateDiagnostic)
    {
        z3950_to_srw_diag(odr_en, sru_res, 
                          records->u.nonSurrogateDiagnostic);
        return false;
    }
    return true;
}


int 
yf::SRUtoZ3950::Impl::z3950_to_srw_diag(mp::odr &odr_en, 
                                       Z_SRW_searchRetrieveResponse *sru_res,
                                       Z_DefaultDiagFormat *ddf) const
{
    int bib1_code = *ddf->condition;
    sru_res->num_diagnostics = 1;
    sru_res->diagnostics = (Z_SRW_diagnostic *)
        odr_malloc(odr_en, sizeof(*sru_res->diagnostics));
    yaz_mk_std_diagnostic(odr_en, sru_res->diagnostics,
                          yaz_diag_bib1_to_srw(bib1_code), 
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
