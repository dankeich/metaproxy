/* $Id: util.cpp,v 1.6 2006-01-17 13:34:51 adam Exp $
   Copyright (c) 2005, Index Data.

%LICENSE%
 */

#include "config.hpp"

#include <yaz/odr.h>
#include <yaz/pquery.h>
#include <yaz/otherinfo.h>
#include "util.hpp"


bool yp2::util::pqf(ODR odr, Z_APDU *apdu, const std::string &q) {
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

int yp2::util::get_vhost_otherinfo(Z_OtherInformation **otherInformation,
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

void yp2::util::split_zurl(std::string zurl, std::string &host,
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

bool yp2::util::set_databases_from_zurl(ODR odr, std::string zurl,
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

yp2::odr::odr(int type)
{
    m_odr = odr_createmem(type);
}

yp2::odr::odr()
{
    m_odr = odr_createmem(ODR_ENCODE);
}

yp2::odr::~odr()
{
    odr_destroy(m_odr);
}

yp2::odr::operator ODR() const
{
    return m_odr;
}

Z_APDU *yp2::odr::create_close(Z_APDU *in_apdu,
                               int reason, const char *addinfo)
{
    Z_APDU *apdu = create_APDU(Z_APDU_close, in_apdu);
    
    *apdu->u.close->closeReason = reason;
    if (addinfo)
        apdu->u.close->diagnosticInformation = odr_strdup(m_odr, addinfo);
    return apdu;
}

Z_APDU *yp2::odr::create_APDU(int type, Z_APDU *in_apdu)
{
    return yp2::util::create_APDU(m_odr, type, in_apdu);
}

Z_APDU *yp2::util::create_APDU(ODR odr, int type, Z_APDU *in_apdu)
{
    Z_APDU *out_apdu = zget_APDU(odr, type);

    Z_ReferenceId **id_to = yp2::util::get_referenceId(out_apdu);
    *id_to = 0;
    if (in_apdu)
    {
        Z_ReferenceId **id_from = yp2::util::get_referenceId(in_apdu);
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
    return out_apdu;
}

Z_APDU *yp2::odr::create_initResponse(Z_APDU *in_apdu,
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

Z_APDU *yp2::odr::create_searchResponse(Z_APDU *in_apdu,
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

Z_APDU *yp2::odr::create_presentResponse(Z_APDU *in_apdu,
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
    }
    return apdu;
}

Z_APDU *yp2::odr::create_scanResponse(Z_APDU *in_apdu,
                                      int error, const char *addinfo)
{
    Z_APDU *apdu = create_APDU(Z_APDU_scanResponse, in_apdu);
    if (error)
    {
        Z_ScanResponse *res = apdu->u.scanResponse;
        res->entries = (Z_ListEntries *) odr_malloc(m_odr, sizeof(*res->entries));
        *res->scanStatus = Z_Scan_failure;

        res->entries->num_entries = 0;
        res->entries->entries = 0;
        res->entries->num_nonsurrogateDiagnostics = 1;
        res->entries->nonsurrogateDiagnostics = (Z_DiagRec **)
            odr_malloc(m_odr, sizeof(Z_DiagRec *));
        res->entries->nonsurrogateDiagnostics[0] = 
            zget_DiagRec(m_odr, error, addinfo);
    }
    return apdu;
}

Z_ReferenceId **yp2::util::get_referenceId(Z_APDU *apdu)
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
