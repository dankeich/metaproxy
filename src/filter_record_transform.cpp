/* This file is part of Metaproxy.
   Copyright (C) 2005-2012 Index Data

Metaproxy is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

Metaproxy is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "config.hpp"
#include "filter_record_transform.hpp"
#include <metaproxy/package.hpp>
#include <metaproxy/util.hpp>
#include "gduutil.hpp"

#include <yaz/diagbib1.h>
#include <yaz/zgdu.h>
#include <yaz/retrieval.h>

#include <iostream>

namespace mp = metaproxy_1;
namespace yf = mp::filter;
namespace mp_util = metaproxy_1::util;

namespace metaproxy_1 {
    namespace filter {
        class RecordTransform::Impl {
        public:
            Impl();
            ~Impl();
            void process(metaproxy_1::Package & package) const;
            void configure(const xmlNode * xml_node, const char *path);
        private:
            yaz_retrieval_t m_retrieval;
        };
    }
}

// define Pimpl wrapper forwarding to Impl
 
yf::RecordTransform::RecordTransform() : m_p(new Impl)
{
}

yf::RecordTransform::~RecordTransform()
{  // must have a destructor because of boost::scoped_ptr
}

void yf::RecordTransform::configure(const xmlNode *xmlnode, bool test_only,
                                    const char *path)
{
    m_p->configure(xmlnode, path);
}

void yf::RecordTransform::process(mp::Package &package) const
{
    m_p->process(package);
}


yf::RecordTransform::Impl::Impl() 
{
    m_retrieval = yaz_retrieval_create();
    assert(m_retrieval);
}

yf::RecordTransform::Impl::~Impl()
{ 
    if (m_retrieval)
        yaz_retrieval_destroy(m_retrieval);
}

void yf::RecordTransform::Impl::configure(const xmlNode *xml_node,
                                          const char *path)
{
    yaz_retrieval_set_path(m_retrieval, path);

    if (!xml_node)
        throw mp::XMLError("RecordTransform filter config: empty XML DOM");

    // parsing down to retrieval node, which can be any of the children nodes
    xmlNode *retrieval_node;
    for (retrieval_node = xml_node->children; 
         retrieval_node; 
         retrieval_node = retrieval_node->next)
    {
        if (retrieval_node->type != XML_ELEMENT_NODE)
            continue;
        if (0 == strcmp((const char *) retrieval_node->name, "retrievalinfo"))
            break;
    }

    // read configuration
    if (0 != yaz_retrieval_configure(m_retrieval, retrieval_node))
    {
        std::string msg("RecordTransform filter config: ");
        msg += yaz_retrieval_get_error(m_retrieval);
        throw mp::XMLError(msg);
    }
}

void yf::RecordTransform::Impl::process(mp::Package &package) const
{

    Z_GDU *gdu_req = package.request().get();
    Z_PresentRequest *pr_req = 0;
    Z_SearchRequest *sr_req = 0;

    const char *input_schema = 0;
    Odr_oid *input_syntax = 0;

    if (gdu_req && gdu_req->which == Z_GDU_Z3950 &&
        gdu_req->u.z3950->which == Z_APDU_presentRequest)
    {
        pr_req = gdu_req->u.z3950->u.presentRequest;

        input_schema =
            mp_util::record_composition_to_esn(pr_req->recordComposition);
        input_syntax = pr_req->preferredRecordSyntax;
    }
    else if (gdu_req && gdu_req->which == Z_GDU_Z3950 &&
             gdu_req->u.z3950->which == Z_APDU_searchRequest)
    {
        sr_req = gdu_req->u.z3950->u.searchRequest;

        input_syntax = sr_req->preferredRecordSyntax;

        // we don't know how many hits we're going to get and therefore
        // the effective element set name.. Therefore we can only allow
        // two cases.. Both equal or absent.. If not, we'll just have to
        // disable the piggyback!
        if (sr_req->smallSetElementSetNames 
            &&
            sr_req->mediumSetElementSetNames
            &&
            sr_req->smallSetElementSetNames->which == Z_ElementSetNames_generic
            && 
            sr_req->mediumSetElementSetNames->which == Z_ElementSetNames_generic
            && 
            !strcmp(sr_req->smallSetElementSetNames->u.generic,
                    sr_req->mediumSetElementSetNames->u.generic))
        {
            input_schema = sr_req->smallSetElementSetNames->u.generic;
        }
        else if (!sr_req->smallSetElementSetNames && 
                 !sr_req->mediumSetElementSetNames)
            ; // input_schema is 0 already
        else
        {
            // disable piggyback (perhaps it was disabled already)
            *sr_req->smallSetUpperBound = 0;
            *sr_req->largeSetLowerBound = 0;
            *sr_req->mediumSetPresentNumber = 0;
            package.move();
            return;
        }
        // we can handle it in record_transform.
    }
    else
    {
        package.move();
        return;
    }
    
    mp::odr odr_en(ODR_ENCODE);

    // setting up variables for conversion state
    yaz_record_conv_t rc = 0;

    const char *match_schema = 0;
    Odr_oid *match_syntax = 0;

    const char *backend_schema = 0;
    Odr_oid *backend_syntax = 0;

    int ret_code 
        = yaz_retrieval_request(m_retrieval,
                                input_schema, input_syntax,
                                &match_schema, &match_syntax,
                                &rc,
                                &backend_schema, &backend_syntax);
    // error handling
    if (ret_code != 0)
    {
        int error_code;
        const char *details = 0;

        if (ret_code == -1) /* error ? */
        {
            details = yaz_retrieval_get_error(m_retrieval);
            error_code = YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS;
        }
        else if (ret_code == 1 || ret_code == 3)
        {
            details = input_schema;
            error_code = YAZ_BIB1_ELEMENT_SET_NAMES_UNSUPP;
        }
        else if (ret_code == 2)
        {
            char oidbuf[OID_STR_MAX];
            oid_oid_to_dotstring(input_syntax, oidbuf);
            details = odr_strdup(odr_en, oidbuf);
            error_code = YAZ_BIB1_RECORD_SYNTAX_UNSUPP;
        }
        else
        {
            char *tmp = (char*) odr_malloc(odr_en, 80);
            sprintf(tmp,
                    "record_transform: yaz_retrieval_get_error returned %d",
                    ret_code);
            details = tmp;
            error_code = YAZ_BIB1_UNSPECIFIED_ERROR;
        }
        Z_APDU *apdu;
        if (sr_req)
        {
            apdu = odr_en.create_searchResponse(
                gdu_req->u.z3950, error_code, details);
        }
        else
        {
            apdu = odr_en.create_presentResponse(
                gdu_req->u.z3950, error_code, details);
        }
        package.response() = apdu;
        return;
    }

    if (sr_req)
    {
        if (backend_syntax) 
            sr_req->preferredRecordSyntax = odr_oiddup(odr_en, backend_syntax);
        else
            sr_req->preferredRecordSyntax = 0;

        if (backend_schema)
        {
            sr_req->smallSetElementSetNames
                = (Z_ElementSetNames *)
                odr_malloc(odr_en, sizeof(Z_ElementSetNames));
            sr_req->smallSetElementSetNames->which = Z_ElementSetNames_generic;
            sr_req->smallSetElementSetNames->u.generic 
                = odr_strdup(odr_en, backend_schema);
            sr_req->mediumSetElementSetNames = sr_req->smallSetElementSetNames;
        }
        else
        {
            sr_req->smallSetElementSetNames = 0;
            sr_req->mediumSetElementSetNames = 0;
        }
    }
    else if (pr_req)
    {
        if (backend_syntax) 
            pr_req->preferredRecordSyntax = odr_oiddup(odr_en, backend_syntax);
        else
            pr_req->preferredRecordSyntax = 0;
        
        if (backend_schema)
        {
            pr_req->recordComposition 
                = (Z_RecordComposition *) 
                odr_malloc(odr_en, sizeof(Z_RecordComposition));
            pr_req->recordComposition->which 
                = Z_RecordComp_simple;
            pr_req->recordComposition->u.simple 
                = (Z_ElementSetNames *)
                odr_malloc(odr_en, sizeof(Z_ElementSetNames));
            pr_req->recordComposition->u.simple->which = Z_ElementSetNames_generic;
            pr_req->recordComposition->u.simple->u.generic 
                = odr_strdup(odr_en, backend_schema);
        }
        else
            pr_req->recordComposition = 0;
    }

    // attaching Z3950 package to filter chain
    package.request() = gdu_req;

    package.move();
    
    Z_GDU *gdu_res = package.response().get();

    // see if we have a records list to patch!
    Z_NamePlusRecordList *records = 0;
    if (gdu_res && gdu_res->which == Z_GDU_Z3950 &&
        gdu_res->u.z3950->which == Z_APDU_presentResponse)
    {
        Z_PresentResponse * pr_res = gdu_res->u.z3950->u.presentResponse;
        
        if (rc && pr_res 
            && pr_res->numberOfRecordsReturned 
            && *(pr_res->numberOfRecordsReturned) > 0
            && pr_res->records
            && pr_res->records->which == Z_Records_DBOSD)
        {
            records = pr_res->records->u.databaseOrSurDiagnostics;
        }
    }
    if (gdu_res && gdu_res->which == Z_GDU_Z3950 &&
        gdu_res->u.z3950->which == Z_APDU_searchResponse)
    {
        Z_SearchResponse *sr_res = gdu_res->u.z3950->u.searchResponse;
        
        if (rc && sr_res 
            && sr_res->numberOfRecordsReturned 
            && *(sr_res->numberOfRecordsReturned) > 0
            && sr_res->records
            && sr_res->records->which == Z_Records_DBOSD)
        {
            records = sr_res->records->u.databaseOrSurDiagnostics;
        }
    }
    
    if (records)
    {
        int i;
        for (i = 0; i < records->num_records; i++)
        {
            Z_NamePlusRecord *npr = records->records[i];
            if (npr->which == Z_NamePlusRecord_databaseRecord)
            {
                WRBUF output_record = wrbuf_alloc();
                Z_External *r = npr->u.databaseRecord;
                int ret_trans = 0;
                if (r->which == Z_External_OPAC)
                {
                    ret_trans =
                        yaz_record_conv_opac_record(rc, r->u.opac,
                                                    output_record);
                }
                else if (r->which == Z_External_octet) 
                {
                    ret_trans =
                        yaz_record_conv_record(rc, (const char *)
                                               r->u.octet_aligned->buf, 
                                               r->u.octet_aligned->len,
                                               output_record);
                }
                if (ret_trans == 0)
                {
                    npr->u.databaseRecord =
                        z_ext_record_oid(odr_en, match_syntax,
                                         wrbuf_buf(output_record),
                                         wrbuf_len(output_record));
                }
                else
                {
                    records->records[i] =
                        zget_surrogateDiagRec(
                            odr_en, npr->databaseName,
                            YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS,
                            yaz_record_conv_get_error(rc));
                }
                wrbuf_destroy(output_record);
            }
        }
        package.response() = gdu_res;
    }
    return;
}

static mp::filter::Base* filter_creator()
{
    return new mp::filter::RecordTransform;
}

extern "C" {
    struct metaproxy_1_filter_struct metaproxy_1_filter_record_transform = {
        0,
        "record_transform",
        filter_creator
    };
}


/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

