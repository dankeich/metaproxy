/* $Id: filter_record_transform.cpp,v 1.2 2006-10-04 11:21:47 marc Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#include "config.hpp"
#include "filter.hpp"
#include "filter_record_transform.hpp"
#include "package.hpp"
#include "util.hpp"
#include "gduutil.hpp"
#include "xmlutil.hpp"

#include <yaz/zgdu.h>
#include <yaz/retrieval.h>

//#include <boost/thread/mutex.hpp>

#include <iostream>

namespace mp = metaproxy_1;
namespace yf = mp::filter;

namespace metaproxy_1 {
    namespace filter {
        class RecordTransform::Impl {
        public:
            Impl();
            ~Impl();
            void process(metaproxy_1::Package & package) const;
            void configure(const xmlNode * xml_node);
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

void yf::RecordTransform::configure(const xmlNode *xmlnode)
{
    m_p->configure(xmlnode);
}

void yf::RecordTransform::process(mp::Package &package) const
{
    m_p->process(package);
}


// define Implementation stuff



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

void yf::RecordTransform::Impl::configure(const xmlNode *xml_node)
{
    //const char *srcdir = getenv("srcdir");
    //if (srcdir)
    //    yaz_retrieval_set_path(m_retrieval, srcdir);

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
    if ( 0 != yaz_retrieval_configure(m_retrieval, retrieval_node)){
        std::string msg("RecordTransform filter config: ");
        msg += yaz_retrieval_get_error(m_retrieval);
        throw mp::XMLError(msg);
    }
}

void yf::RecordTransform::Impl::process(mp::Package &package) const
{

    Z_GDU *gdu_req = package.request().get();
    
    // only working on z3950 present packages
    if (!gdu_req 
        || !(gdu_req->which == Z_GDU_Z3950) 
        || !(gdu_req->u.z3950->which == Z_APDU_presentRequest))
    {
        package.move();
        return;
    }
    
    // getting original present request
    Z_PresentRequest *pr = gdu_req->u.z3950->u.presentRequest;

    // setting up ODR's for memory during encoding/decoding
    //mp::odr odr_de(ODR_DECODE);  
    mp::odr odr_en(ODR_ENCODE);

    // now re-insructing the z3950 backend present request
     
    // z3950'fy record syntax
    //Odr_oid odr_oid;
    if(pr->preferredRecordSyntax){
        (pr->preferredRecordSyntax)
            = yaz_str_to_z3950oid(odr_en, CLASS_RECSYN, "xml");
        
        // = yaz_oidval_to_z3950oid (odr_en, CLASS_RECSYN, VAL_TEXT_XML);
    }
    // Odr_oid *yaz_str_to_z3950oid (ODR o, int oid_class,
    //                                         const char *str);
    // const char *yaz_z3950oid_to_str (Odr_oid *oid, int *oid_class);


    // z3950'fy record schema
    //if ()
    // {
    //     pr->recordComposition 
    //         = (Z_RecordComposition *) 
    //           odr_malloc(odr_en, sizeof(Z_RecordComposition));
    //     pr->recordComposition->which 
    //         = Z_RecordComp_simple;
    //     pr->recordComposition->u.simple 
    //         = build_esn_from_schema(odr_en, 
    //                                 (const char *) sr_req->recordSchema); 
    // }

    // attaching Z3950 package to filter chain
    package.request() = gdu_req;

    // std::cout << "z3950_present_request " << *apdu << "\n";   

    // sending package
    package.move();

   //check successful Z3950 present response
    Z_GDU *gdu_res = package.response().get();
    if (!gdu_res || gdu_res->which != Z_GDU_Z3950 
        || gdu_res->u.z3950->which != Z_APDU_presentResponse
        || !gdu_res->u.z3950->u.presentResponse)

    {
        std::cout << "record-transform: error back present\n";
        package.session().close();
        return;
    }
    

    // everything fine, continuing
    // std::cout << "z3950_present_request OK\n";
    // std::cout << "back z3950 " << *gdu_res << "\n";


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
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
