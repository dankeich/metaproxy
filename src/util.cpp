/* $Id: util.cpp,v 1.20 2006-09-29 08:42:47 marc Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#include "config.hpp"
#include "util.hpp"

#include <yaz/odr.h>
#include <yaz/pquery.h>
#include <yaz/otherinfo.h>
#include <yaz/querytowrbuf.h> // for yaz_query_to_wrbuf()

#include <iostream>

namespace mp = metaproxy_1;

// Doxygen doesn't like mp::util, so we use this instead
namespace mp_util = metaproxy_1::util;


std::string mp_util::http_header_value(const Z_HTTP_Header* header, 
                                       const std::string name)
{
    while (header && header->name
           && std::string(header->name) !=  name)
        header = header->next;
    
    if (header && header->name && std::string(header->name) == name
        && header->value)
        return std::string(header->value);
    
    return std::string();
}
    


int mp_util::memcmp2(const void *buf1, int len1,
                     const void *buf2, int len2)
{
    int d = len1 - len2;

    // compare buffer (common length)
    int c = memcmp(buf1, buf2, d > 0 ? len2 : len1);
    if (c > 0)
        return 1;
    else if (c < 0)
        return -1;
    
    // compare (remaining bytes)
    if (d > 0)
        return 1;
    else if (d < 0)
        return -1;
    return 0;
}


std::string mp_util::database_name_normalize(const std::string &s)
{
    std::string r = s;
    size_t i;
    for (i = 0; i < r.length(); i++)
    {
        int ch = r[i];
        if (ch >= 'A' && ch <= 'Z')
            r[i] = ch + 'a' - 'A';
    }
    return r;

}

void mp_util::piggyback(int smallSetUpperBound,
                                  int largeSetLowerBound,
                                  int mediumSetPresentNumber,
                                  int result_set_size,
                                  int &number_to_present)
{
    // deal with piggyback

    if (result_set_size < smallSetUpperBound)
    {
        // small set . Return all records in set
        number_to_present = result_set_size;
    }
    else if (result_set_size > largeSetLowerBound)
    {
        // large set . Return no records
        number_to_present = 0;
    }
    else
    {
        // medium set . Return mediumSetPresentNumber records
        number_to_present = mediumSetPresentNumber;
        if (number_to_present > result_set_size)
            number_to_present = result_set_size;
    }
}


bool mp_util::pqf(ODR odr, Z_APDU *apdu, const std::string &q)
{
    YAZ_PQF_Parser pqf_parser = yaz_pqf_create();
    
    Z_RPNQuery *rpn = yaz_pqf_parse(pqf_parser, odr, q.c_str());
    if (!rpn)
    {
	yaz_pqf_destroy(pqf_parser);
	return false;
    }
    yaz_pqf_destroy(pqf_parser);
    Z_Query *query = (Z_Query *) odr_malloc(odr, sizeof(Z_Query));
    query->which = Z_Query_type_1;
    query->u.type_1 = rpn;
    
    apdu->u.searchRequest->query = query;
    return true;
}


std::string mp_util::zQueryToString(Z_Query *query)
{
    std::string query_str = "";

    if (query && query->which == Z_Query_type_1){
        Z_RPNQuery *rpn = query->u.type_1;
        
        if (rpn){
            
            // allocate wrbuf (strings in YAZ!)
            WRBUF w = wrbuf_alloc();
            
            // put query in w
            yaz_rpnquery_to_wrbuf(w, rpn);
            
            // from w to std::string
            query_str = std::string(wrbuf_buf(w), wrbuf_len(w));
            
            // destroy wrbuf
            wrbuf_free(w, 1);
        }
    }

#if 0
    if (query && query->which == Z_Query_type_1){
        
        // allocate wrbuf (strings in YAZ!)
        WRBUF w = wrbuf_alloc();
        
        // put query in w
        yaz_query_to_wrbuf(w, query);
        
        // from w to std::string
        query_str = std::string(wrbuf_buf(w), wrbuf_len(w));
        
        // destroy wrbuf
        wrbuf_free(w, 1);
    }    
#endif
    return query_str;
}

void mp_util::get_default_diag(Z_DefaultDiagFormat *r,
                                         int &error_code, std::string &addinfo)
{
    error_code = *r->condition;
    switch (r->which)
    {
    case Z_DefaultDiagFormat_v2Addinfo:
        addinfo = std::string(r->u.v2Addinfo);
        break;
    case Z_DefaultDiagFormat_v3Addinfo:
        addinfo = r->u.v3Addinfo;
        break;
    }
}

void mp_util::get_init_diagnostics(
    Z_InitResponse *initrs, int &error_code, std::string &addinfo)
{
    Z_External *uif = initrs->userInformationField;
    
    if (uif && uif->which == Z_External_userInfo1)
    {
        Z_OtherInformation *ui = uif->u.userInfo1;
        int i;
        for (i = 0; i < ui->num_elements; i++)
        {
            Z_OtherInformationUnit *unit = ui->list[i];
            if (unit->which == Z_OtherInfo_externallyDefinedInfo &&
                unit->information.externallyDefinedInfo &&
                unit->information.externallyDefinedInfo->which ==
                Z_External_diag1) 
            {
                Z_DiagnosticFormat *diag = 
                    unit->information.externallyDefinedInfo->u.diag1;

                if (diag->num > 0)
                {
                    Z_DiagnosticFormat_s *ds = diag->elements[0];
                    if (ds->which == Z_DiagnosticFormat_s_defaultDiagRec)
                        mp::util::get_default_diag(ds->u.defaultDiagRec,
                                                    error_code, addinfo);
                }
            } 
        }
    }
}

int mp_util::get_or_remove_vhost_otherinfo(
    Z_OtherInformation **otherInformation,
    bool remove_flag,
    std::list<std::string> &vhosts)
{
    int cat;
    for (cat = 1; ; cat++)
    {
        // check virtual host
        const char *vhost =
            yaz_oi_get_string_oidval(otherInformation,
                                     VAL_PROXY, 
                                     cat /* categoryValue */,
                                     remove_flag /* delete flag */);
        if (!vhost)
            break;
        vhosts.push_back(std::string(vhost));
    }
    --cat;
    return cat;
}

void mp_util::get_vhost_otherinfo(
    Z_OtherInformation *otherInformation,
    std::list<std::string> &vhosts)
{
    get_or_remove_vhost_otherinfo(&otherInformation, false, vhosts);
}

int mp_util::remove_vhost_otherinfo(
    Z_OtherInformation **otherInformation,
    std::list<std::string> &vhosts)
{
    return get_or_remove_vhost_otherinfo(otherInformation, true, vhosts);
}

void mp_util::set_vhost_otherinfo(
    Z_OtherInformation **otherInformation, ODR odr,
    const std::list<std::string> &vhosts)
{
    int cat;
    std::list<std::string>::const_iterator it = vhosts.begin();
    for (cat = 1; it != vhosts.end() ; cat++, it++)
    {
        yaz_oi_set_string_oidval(otherInformation, odr,
                                 VAL_PROXY, cat, it->c_str());
    }
}

void mp_util::split_zurl(std::string zurl, std::string &host,
                                   std::list<std::string> &db)
{
    const char *zurl_cstr = zurl.c_str();
    const char *sep = strchr(zurl_cstr, '/');
    
    if (sep)
    {
        host = std::string(zurl_cstr, sep - zurl_cstr);

        const char *cp1 = sep+1;
        while(1)
        {
            const char *cp2 = strchr(cp1, '+');
            if (cp2)
                db.push_back(std::string(cp1, cp2 - cp1));
            else
            {
                db.push_back(std::string(cp1));
                break;
            }
            cp1 = cp2+1;
        }
    }
    else
    {
        host = zurl;
    }
}

bool mp_util::set_databases_from_zurl(
    ODR odr, std::string zurl,
    int *db_num, char ***db_strings)
{
    std::string host;
    std::list<std::string> dblist;

    split_zurl(zurl, host, dblist);
   
    if (dblist.size() == 0)
        return false;
    *db_num = dblist.size();
    *db_strings = (char **) odr_malloc(odr, sizeof(char*) * (*db_num));

    std::list<std::string>::const_iterator it = dblist.begin();
    for (int i = 0; it != dblist.end(); it++, i++)
        (*db_strings)[i] = odr_strdup(odr, it->c_str());
    return true;
}

mp::odr::odr(int type)
{
    m_odr = odr_createmem(type);
}

mp::odr::odr()
{
    m_odr = odr_createmem(ODR_ENCODE);
}

mp::odr::~odr()
{
    odr_destroy(m_odr);
}

mp::odr::operator ODR() const
{
    return m_odr;
}

Z_APDU *mp::odr::create_close(const Z_APDU *in_apdu,
                              int reason, const char *addinfo)
{
    Z_APDU *apdu = create_APDU(Z_APDU_close, in_apdu);
    
    *apdu->u.close->closeReason = reason;
    if (addinfo)
        apdu->u.close->diagnosticInformation = odr_strdup(m_odr, addinfo);
    return apdu;
}

Z_APDU *mp::odr::create_APDU(int type, const Z_APDU *in_apdu)
{
    return mp::util::create_APDU(m_odr, type, in_apdu);
}

Z_APDU *mp_util::create_APDU(ODR odr, int type, const Z_APDU *in_apdu)
{
    Z_APDU *out_apdu = zget_APDU(odr, type);
    transfer_referenceId(odr, in_apdu, out_apdu);
    return out_apdu;
}

void mp_util::transfer_referenceId(ODR odr, const Z_APDU *src, Z_APDU *dst)
{
    Z_ReferenceId **id_to = mp::util::get_referenceId(dst);
    *id_to = 0;
    if (src)
    {
        Z_ReferenceId **id_from = mp::util::get_referenceId(src);
        if (id_from && *id_from && id_to)
        {
            *id_to = (Z_ReferenceId*) odr_malloc (odr, sizeof(**id_to));
            (*id_to)->size = (*id_to)->len = (*id_from)->len;
            (*id_to)->buf = (unsigned char*) odr_malloc(odr, (*id_to)->len);
            memcpy((*id_to)->buf, (*id_from)->buf, (*id_to)->len);
        }
        else if (id_to)
            *id_to = 0;
    }
}

Z_APDU *mp::odr::create_initResponse(const Z_APDU *in_apdu,
                                     int error, const char *addinfo)
{
    Z_APDU *apdu = create_APDU(Z_APDU_initResponse, in_apdu);
    if (error)
    {
        apdu->u.initResponse->userInformationField =
            zget_init_diagnostics(m_odr, error, addinfo);
        *apdu->u.initResponse->result = 0;
    }
    return apdu;
}

Z_APDU *mp::odr::create_searchResponse(const Z_APDU *in_apdu,
                                       int error, const char *addinfo)
{
    Z_APDU *apdu = create_APDU(Z_APDU_searchResponse, in_apdu);
    if (error)
    {
        Z_Records *rec = (Z_Records *) odr_malloc(m_odr, sizeof(Z_Records));
        *apdu->u.searchResponse->searchStatus = 0;
        apdu->u.searchResponse->records = rec;
        rec->which = Z_Records_NSD;
        rec->u.nonSurrogateDiagnostic =
            zget_DefaultDiagFormat(m_odr, error, addinfo);
        
    }
    return apdu;
}

Z_APDU *mp::odr::create_presentResponse(const Z_APDU *in_apdu,
                                        int error, const char *addinfo)
{
    Z_APDU *apdu = create_APDU(Z_APDU_presentResponse, in_apdu);
    if (error)
    {
        Z_Records *rec = (Z_Records *) odr_malloc(m_odr, sizeof(Z_Records));
        apdu->u.presentResponse->records = rec;
        
        rec->which = Z_Records_NSD;
        rec->u.nonSurrogateDiagnostic =
            zget_DefaultDiagFormat(m_odr, error, addinfo);
        *apdu->u.presentResponse->presentStatus = Z_PresentStatus_failure;
    }
    return apdu;
}

Z_APDU *mp::odr::create_scanResponse(const Z_APDU *in_apdu,
                                     int error, const char *addinfo)
{
    Z_APDU *apdu = create_APDU(Z_APDU_scanResponse, in_apdu);
    Z_ScanResponse *res = apdu->u.scanResponse;
    res->entries = (Z_ListEntries *) odr_malloc(m_odr, sizeof(*res->entries));
    res->entries->num_entries = 0;
    res->entries->entries = 0;

    if (error)
    {
        *res->scanStatus = Z_Scan_failure;

        res->entries->num_nonsurrogateDiagnostics = 1;
        res->entries->nonsurrogateDiagnostics = (Z_DiagRec **)
            odr_malloc(m_odr, sizeof(Z_DiagRec *));
        res->entries->nonsurrogateDiagnostics[0] = 
            zget_DiagRec(m_odr, error, addinfo);
    }
    else
    {
        res->entries->num_nonsurrogateDiagnostics = 0;
        res->entries->nonsurrogateDiagnostics = 0;
    }
    return apdu;
}

Z_GDU *mp::odr::create_HTTP_Response(mp::Session &session,
                                     Z_HTTP_Request *hreq, int code)
{
    const char *response_version = "1.0";
    bool keepalive = false;
    if (!strcmp(hreq->version, "1.0")) 
    {
        const char *v = z_HTTP_header_lookup(hreq->headers, "Connection");
        if (v && !strcmp(v, "Keep-Alive"))
            keepalive = true;
        else
            session.close();
        response_version = "1.0";
    }
    else
    {
        const char *v = z_HTTP_header_lookup(hreq->headers, "Connection");
        if (v && !strcmp(v, "close"))
            session.close();
        else
            keepalive = true;
        response_version = "1.1";
    }

    Z_GDU *gdu = z_get_HTTP_Response(m_odr, code);
    Z_HTTP_Response *hres = gdu->u.HTTP_Response;
    hres->version = odr_strdup(m_odr, response_version);
    if (keepalive)
        z_HTTP_header_add(m_odr, &hres->headers, "Connection", "Keep-Alive");
    
    return gdu;
}

Z_ReferenceId **mp_util::get_referenceId(const Z_APDU *apdu)
{
    switch (apdu->which)
    {
    case  Z_APDU_initRequest:
        return &apdu->u.initRequest->referenceId; 
    case  Z_APDU_initResponse:
        return &apdu->u.initResponse->referenceId;
    case  Z_APDU_searchRequest:
        return &apdu->u.searchRequest->referenceId;
    case  Z_APDU_searchResponse:
        return &apdu->u.searchResponse->referenceId;
    case  Z_APDU_presentRequest:
        return &apdu->u.presentRequest->referenceId;
    case  Z_APDU_presentResponse:
        return &apdu->u.presentResponse->referenceId;
    case  Z_APDU_deleteResultSetRequest:
        return &apdu->u.deleteResultSetRequest->referenceId;
    case  Z_APDU_deleteResultSetResponse:
        return &apdu->u.deleteResultSetResponse->referenceId;
    case  Z_APDU_accessControlRequest:
        return &apdu->u.accessControlRequest->referenceId;
    case  Z_APDU_accessControlResponse:
        return &apdu->u.accessControlResponse->referenceId;
    case  Z_APDU_resourceControlRequest:
        return &apdu->u.resourceControlRequest->referenceId;
    case  Z_APDU_resourceControlResponse:
        return &apdu->u.resourceControlResponse->referenceId;
    case  Z_APDU_triggerResourceControlRequest:
        return &apdu->u.triggerResourceControlRequest->referenceId;
    case  Z_APDU_resourceReportRequest:
        return &apdu->u.resourceReportRequest->referenceId;
    case  Z_APDU_resourceReportResponse:
        return &apdu->u.resourceReportResponse->referenceId;
    case  Z_APDU_scanRequest:
        return &apdu->u.scanRequest->referenceId;
    case  Z_APDU_scanResponse:
        return &apdu->u.scanResponse->referenceId;
    case  Z_APDU_sortRequest:
        return &apdu->u.sortRequest->referenceId;
    case  Z_APDU_sortResponse:
        return &apdu->u.sortResponse->referenceId;
    case  Z_APDU_segmentRequest:
        return &apdu->u.segmentRequest->referenceId;
    case  Z_APDU_extendedServicesRequest:
        return &apdu->u.extendedServicesRequest->referenceId;
    case  Z_APDU_extendedServicesResponse:
        return &apdu->u.extendedServicesResponse->referenceId;
    case  Z_APDU_close:
        return &apdu->u.close->referenceId;
    }
    return 0;
}


/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
