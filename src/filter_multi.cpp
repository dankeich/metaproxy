/* $Id: filter_multi.cpp,v 1.3 2006-01-16 15:51:56 adam Exp $
   Copyright (c) 2005, Index Data.

%LICENSE%
 */

#include "config.hpp"

#include "filter.hpp"
#include "package.hpp"

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/shared_ptr.hpp>

#include "util.hpp"
#include "filter_multi.hpp"

#include <yaz/zgdu.h>
#include <yaz/otherinfo.h>
#include <yaz/diagbib1.h>

#include <map>
#include <iostream>

namespace yf = yp2::filter;

namespace yp2 {
    namespace filter {

        struct Multi::BackendSet {
            BackendPtr m_backend;
            int m_count;
            bool operator < (const BackendSet &k) const;
        };
        struct Multi::FrontendSet {
            struct PresentJob {
                BackendPtr m_backend;
                int m_pos;
                int m_inside_pos;
            };
            FrontendSet(std::string setname);
            FrontendSet();
            ~FrontendSet();

            void round_robin(int pos, int number, std::list<PresentJob> &job);

            std::list<BackendSet> m_backend_sets;
            std::string m_setname;
        };
        struct Multi::Backend {
            PackagePtr m_package;
            std::string m_backend_database;
            std::string m_vhost;
            std::string m_route;
            void operator() (void);  // thread operation
        };
        struct Multi::Frontend {
            Frontend(Rep *rep);
            ~Frontend();
            yp2::Session m_session;
            bool m_is_multi;
            bool m_in_use;
            std::list<BackendPtr> m_backend_list;
            std::map<std::string,Multi::FrontendSet> m_sets;

            void multi_move(std::list<BackendPtr> &blist);
            void init(Package &package, Z_GDU *gdu);
            void close(Package &package);
            void search(Package &package, Z_APDU *apdu);
            void present(Package &package, Z_APDU *apdu);
            Rep *m_p;
        };            
        struct Multi::Map {
            Map(std::list<std::string> hosts, std::string route);
            Map();
            std::list<std::string> m_hosts;
            std::string m_route;
        };
        class Multi::Rep {
            friend class Multi;
            friend class Frontend;
            
            FrontendPtr get_frontend(Package &package);
            void release_frontend(Package &package);
        private:
            boost::mutex m_sessions_mutex;
            std::map<std::string, Multi::Map>m_maps;

            boost::mutex m_mutex;
            boost::condition m_cond_session_ready;
            std::map<yp2::Session, FrontendPtr> m_clients;
        };
    }
}

using namespace yp2;

bool yf::Multi::BackendSet::operator < (const BackendSet &k) const
{
    return m_count < k.m_count;
}

yf::Multi::Frontend::Frontend(Rep *rep)
{
    m_p = rep;
    m_is_multi = false;
}

yf::Multi::Frontend::~Frontend()
{
}

yf::Multi::FrontendPtr yf::Multi::Rep::get_frontend(Package &package)
{
    boost::mutex::scoped_lock lock(m_mutex);

    std::map<yp2::Session,yf::Multi::FrontendPtr>::iterator it;
    
    while(true)
    {
        it = m_clients.find(package.session());
        if (it == m_clients.end())
            break;
        
        if (!it->second->m_in_use)
        {
            it->second->m_in_use = true;
            return it->second;
        }
        m_cond_session_ready.wait(lock);
    }
    FrontendPtr f(new Frontend(this));
    m_clients[package.session()] = f;
    f->m_in_use = true;
    return f;
}

void yf::Multi::Rep::release_frontend(Package &package)
{
    boost::mutex::scoped_lock lock(m_mutex);
    std::map<yp2::Session,yf::Multi::FrontendPtr>::iterator it;
    
    it = m_clients.find(package.session());
    if (it != m_clients.end())
    {
        if (package.session().is_closed())
        {
            it->second->close(package);
            m_clients.erase(it);
        }
        else
        {
            it->second->m_in_use = false;
        }
        m_cond_session_ready.notify_all();
    }
}

yf::Multi::FrontendSet::FrontendSet(std::string setname)
    :  m_setname(setname)
{
}


yf::Multi::FrontendSet::FrontendSet()
{
}


yf::Multi::FrontendSet::~FrontendSet()
{
}

yf::Multi::Map::Map(std::list<std::string> hosts, std::string route)
    : m_hosts(hosts), m_route(route) 
{
}

yf::Multi::Map::Map()
{
}

yf::Multi::Multi() : m_p(new Multi::Rep)
{
}

yf::Multi::~Multi() {
}


void yf::Multi::add_map_host2hosts(std::string host,
                                   std::list<std::string> hosts,
                                   std::string route)
{
    m_p->m_maps[host] = Multi::Map(hosts, route);
}

void yf::Multi::Backend::operator() (void) 
{
    m_package->move(m_route);
}

void yf::Multi::Frontend::close(Package &package)
{
    std::list<BackendPtr>::const_iterator bit;
    for (bit = m_backend_list.begin(); bit != m_backend_list.end(); bit++)
    {
        BackendPtr b = *bit;

        b->m_package->copy_filter(package);
        b->m_package->request() = (Z_GDU *) 0;
        b->m_package->session().close();
        b->m_package->move(b->m_route);
    }
}

void yf::Multi::Frontend::multi_move(std::list<BackendPtr> &blist)
{
    std::list<BackendPtr>::const_iterator bit;
    boost::thread_group g;
    for (bit = blist.begin(); bit != blist.end(); bit++)
    {
        g.add_thread(new boost::thread(**bit));
    }
    g.join_all();
}


void yf::Multi::FrontendSet::round_robin(int start, int number,
                                         std::list<PresentJob> &jobs)
{
    int fetched = 0;
    int p = 1;
    bool eof = true;

    std::list<int> pos;
    std::list<int> inside_pos;
    std::list<BackendSet>::const_iterator bsit;
    for (bsit = m_backend_sets.begin(); bsit != m_backend_sets.end(); bsit++)
    {
        pos.push_back(1);
        inside_pos.push_back(0);
    }

    std::list<int>::iterator psit = pos.begin();
    std::list<int>::iterator esit = inside_pos.begin();
    bsit = m_backend_sets.begin();
    while (fetched < number)
    {
        if (bsit == m_backend_sets.end())
        {
            psit = pos.begin();
            esit = inside_pos.begin();
            bsit = m_backend_sets.begin();
            if (eof)
                break;
            eof = true;
        }
        if (*psit <= bsit->m_count)
        {
            if (p >= start)
            {
                PresentJob job;
                job.m_backend = bsit->m_backend;
                job.m_pos = *psit;
                job.m_inside_pos = *esit;
                jobs.push_back(job);
                (*esit)++;
                fetched++;
            }
            (*psit)++;
            p++;
            eof = false;
        }
        psit++;
        esit++;
        bsit++;
    }
}

void yf::Multi::Frontend::init(Package &package, Z_GDU *gdu)
{
    Z_InitRequest *req = gdu->u.z3950->u.initRequest;

    std::list<std::string> targets;

    int no_targets = 0;
    while (true)
    {
        const char *vhost_cstr =
            yaz_oi_get_string_oidval(&req->otherInfo, VAL_PROXY, no_targets+1,
                                     0);
        if (!vhost_cstr)
            break;
        no_targets++;
        if (no_targets > 1000)
            return;
        targets.push_back(vhost_cstr);
    }
    if (no_targets < 2)
    {
        package.move();
        return;
    }

    std::list<std::string>::const_iterator t_it = targets.begin();
    for (; t_it != targets.end(); t_it++)
    {
        Session s;
        Backend *b = new Backend;
        b->m_vhost = *t_it;

        // b->m_route unset
        b->m_package = PackagePtr(new Package(s, package.origin()));

        m_backend_list.push_back(BackendPtr(b));
    }
    m_is_multi = true;

    // create init request 
    std::list<BackendPtr>::const_iterator bit;
    for (bit = m_backend_list.begin(); bit != m_backend_list.end(); bit++)
    {
        yp2::odr odr;
        BackendPtr b = *bit;
        Z_APDU *init_apdu = zget_APDU(odr, Z_APDU_initRequest);
        
        yaz_oi_set_string_oidval(&init_apdu->u.initRequest->otherInfo, odr,
                                 VAL_PROXY, 1, b->m_vhost.c_str());
        
        Z_InitRequest *req = init_apdu->u.initRequest;
        
        ODR_MASK_SET(req->options, Z_Options_search);
        ODR_MASK_SET(req->options, Z_Options_present);
        ODR_MASK_SET(req->options, Z_Options_namedResultSets);
        
        ODR_MASK_SET(req->protocolVersion, Z_ProtocolVersion_1);
        ODR_MASK_SET(req->protocolVersion, Z_ProtocolVersion_2);
        ODR_MASK_SET(req->protocolVersion, Z_ProtocolVersion_3);
        
        b->m_package->request() = init_apdu;

        b->m_package->copy_filter(package);
    }
    multi_move(m_backend_list);

    // create the frontend init response based on each backend init response
    yp2::odr odr;

    Z_APDU *f_apdu = odr.create_initResponse(gdu->u.z3950, 0, 0);
    Z_InitResponse *f_resp = f_apdu->u.initResponse;

    ODR_MASK_SET(f_resp->options, Z_Options_search);
    ODR_MASK_SET(f_resp->options, Z_Options_present);
    ODR_MASK_SET(f_resp->options, Z_Options_namedResultSets);
    
    ODR_MASK_SET(f_resp->protocolVersion, Z_ProtocolVersion_1);
    ODR_MASK_SET(f_resp->protocolVersion, Z_ProtocolVersion_2);
    ODR_MASK_SET(f_resp->protocolVersion, Z_ProtocolVersion_3);

    for (bit = m_backend_list.begin(); bit != m_backend_list.end(); bit++)
    {
        PackagePtr p = (*bit)->m_package;
        
        if (p->session().is_closed()) // if any backend closes, close frontend
            package.session().close();
        Z_GDU *gdu = p->response().get();
        if (gdu && gdu->which == Z_GDU_Z3950 && gdu->u.z3950->which ==
            Z_APDU_initResponse)
        {
            int i;
            Z_APDU *b_apdu = gdu->u.z3950;
            Z_InitResponse *b_resp = b_apdu->u.initResponse;

            // common options for all backends
            for (i = 0; i <= Z_Options_stringSchema; i++)
            {
                if (!ODR_MASK_GET(b_resp->options, i))
                    ODR_MASK_CLEAR(f_resp->options, i);
            }
            // common protocol version
            for (i = 0; i <= Z_ProtocolVersion_3; i++)
                if (!ODR_MASK_GET(b_resp->protocolVersion, i))
                    ODR_MASK_CLEAR(f_resp->protocolVersion, i);
            // reject if any of the backends reject
            if (!*b_resp->result)
                *f_resp->result = 0;
        }
        else
        {
            // if any target does not return init return that (close or
            // similar )
            package.response() = p->response();
            return;
        }
    }
    package.response() = f_apdu;
}

void yf::Multi::Frontend::search(Package &package, Z_APDU *apdu_req)
{
    // create search request 
    Z_SearchRequest *req = apdu_req->u.searchRequest;
        
    // deal with piggy back (for now disable)
    *req->smallSetUpperBound = 0;
    *req->largeSetLowerBound = 1;
    *req->mediumSetPresentNumber = 1;

    int default_num_db = req->num_databaseNames;
    char **default_db = req->databaseNames;

    std::list<BackendPtr>::const_iterator bit;
    for (bit = m_backend_list.begin(); bit != m_backend_list.end(); bit++)
    {
        PackagePtr p = (*bit)->m_package;
        yp2::odr odr;
    
        if (!yp2::util::set_databases_from_zurl(odr, (*bit)->m_vhost,
                                                &req->num_databaseNames,
                                                &req->databaseNames))
        {
            req->num_databaseNames = default_num_db;
            req->databaseNames = default_db;
        }
        p->request() = apdu_req;
        p->copy_filter(package);
    }
    multi_move(m_backend_list);

    // look at each response
    FrontendSet resultSet(std::string(req->resultSetName));

    int total_count = 0;
    Z_Records *z_records_diag = 0;  // no diagnostics (yet)
    for (bit = m_backend_list.begin(); bit != m_backend_list.end(); bit++)
    {
        PackagePtr p = (*bit)->m_package;
        
        if (p->session().is_closed()) // if any backend closes, close frontend
            package.session().close();
        
        Z_GDU *gdu = p->response().get();
        if (gdu && gdu->which == Z_GDU_Z3950 && gdu->u.z3950->which ==
            Z_APDU_searchResponse)
        {
            Z_APDU *b_apdu = gdu->u.z3950;
            Z_SearchResponse *b_resp = b_apdu->u.searchResponse;
         
            // see we get any errors (AKA diagnstics)
            if (b_resp->records)
            {
                if (b_resp->records->which == Z_Records_NSD
                    || b_resp->records->which == Z_Records_multipleNSD)
                    z_records_diag = b_resp->records;
                // we may set this multiple times (TOO BAD!)
            }
            BackendSet backendSet;
            backendSet.m_backend = *bit;
            backendSet.m_count = *b_resp->resultCount;
            total_count += *b_resp->resultCount;
            resultSet.m_backend_sets.push_back(backendSet);
        }
        else
        {
            // if any target does not return search response - return that 
            package.response() = p->response();
            return;
        }
    }

    yp2::odr odr;
    Z_APDU *f_apdu = odr.create_searchResponse(apdu_req, 0, 0);
    Z_SearchResponse *f_resp = f_apdu->u.searchResponse;

    if (z_records_diag)
    {
        // search error
        f_resp->records = z_records_diag;
    }
    else
    {   // assume OK
        m_sets[resultSet.m_setname] = resultSet;
    }
    *f_resp->resultCount = total_count;
    
    package.response() = f_apdu;
}

void yf::Multi::Frontend::present(Package &package, Z_APDU *apdu_req)
{
    // create present request 
    Z_PresentRequest *req = apdu_req->u.presentRequest;

    Sets_it it;
    it = m_sets.find(std::string(req->resultSetId));
    if (it == m_sets.end())
    {
        yp2::odr odr;
        Z_APDU *apdu = 
            odr.create_presentResponse(
                apdu_req,
                YAZ_BIB1_SPECIFIED_RESULT_SET_DOES_NOT_EXIST,
                req->resultSetId);
        package.response() = apdu;
        return;
    }
    std::list<Multi::FrontendSet::PresentJob> jobs;
    int start = *req->resultSetStartPoint;
    int number = *req->numberOfRecordsRequested;
    it->second.round_robin(start, number, jobs);

    std::list<BackendPtr> present_backend_list;

    std::list<BackendSet>::const_iterator bsit;
    bsit = it->second.m_backend_sets.begin();
    for (; bsit != it->second.m_backend_sets.end(); bsit++)
    {
        std::list<Multi::FrontendSet::PresentJob>::const_iterator jit;
        int start = -1;
        int end = -1;
        
        for (jit = jobs.begin(); jit != jobs.end(); jit++)
        {
            if (jit->m_backend == bsit->m_backend)
            {
                if (start == -1 || jit->m_pos < start)
                    start = jit->m_pos;
                if (end == -1 || jit->m_pos > end)
                    end = jit->m_pos;
            }
        }
        if (start != -1)
        {
            PackagePtr p = bsit->m_backend->m_package;

            *req->resultSetStartPoint = start;
            *req->numberOfRecordsRequested = end - start + 1;
            
            p->request() = apdu_req;
            p->copy_filter(package);

            present_backend_list.push_back(bsit->m_backend);
        }
    }
    multi_move(present_backend_list);

    // look at each response
    Z_Records *z_records_diag = 0;

    std::list<BackendPtr>::const_iterator pbit = present_backend_list.begin();
    for (; pbit != present_backend_list.end(); pbit++)
    {
        PackagePtr p = (*pbit)->m_package;
        
        if (p->session().is_closed()) // if any backend closes, close frontend
            package.session().close();
        
        Z_GDU *gdu = p->response().get();
        if (gdu && gdu->which == Z_GDU_Z3950 && gdu->u.z3950->which ==
            Z_APDU_presentResponse)
        {
            Z_APDU *b_apdu = gdu->u.z3950;
            Z_PresentResponse *b_resp = b_apdu->u.presentResponse;
         
            // see we get any errors (AKA diagnstics)
            if (b_resp->records)
            {
                if (b_resp->records->which != Z_Records_DBOSD)
                    z_records_diag = b_resp->records;
                // we may set this multiple times (TOO BAD!)
            }
        }
        else
        {
            // if any target does not return present response - return that 
            package.response() = p->response();
            return;
        }
    }

    yp2::odr odr;
    Z_APDU *f_apdu = odr.create_presentResponse(apdu_req, 0, 0);
    Z_PresentResponse *f_resp = f_apdu->u.presentResponse;

    if (z_records_diag)
    {
        f_resp->records = z_records_diag;
        *f_resp->presentStatus = Z_PresentStatus_failure;
    }
    else
    {
        f_resp->records = (Z_Records *) odr_malloc(odr, sizeof(Z_Records));
        Z_Records * records = f_resp->records;
        records->which = Z_Records_DBOSD;
        records->u.databaseOrSurDiagnostics =
            (Z_NamePlusRecordList *)
            odr_malloc(odr, sizeof(Z_NamePlusRecordList));
        Z_NamePlusRecordList *nprl = records->u.databaseOrSurDiagnostics;
        nprl->num_records = jobs.size();
        nprl->records = (Z_NamePlusRecord**)
            odr_malloc(odr, sizeof(Z_NamePlusRecord *) * nprl->num_records);
        int i = 0;
        std::list<Multi::FrontendSet::PresentJob>::const_iterator jit;
        for (jit = jobs.begin(); jit != jobs.end(); jit++)
        {
            PackagePtr p = jit->m_backend->m_package;
            
            Z_GDU *gdu = p->response().get();
            Z_APDU *b_apdu = gdu->u.z3950;
            Z_PresentResponse *b_resp = b_apdu->u.presentResponse;

            nprl->records[i++] =
                b_resp->records->u.databaseOrSurDiagnostics->
                records[jit->m_inside_pos];
        }
        *f_resp->nextResultSetPosition = start + i;
        *f_resp->numberOfRecordsReturned = i;
    }
    package.response() = f_apdu;
}

void yf::Multi::process(Package &package) const
{
    FrontendPtr f = m_p->get_frontend(package);

    Z_GDU *gdu = package.request().get();
    
    if (gdu && gdu->which == Z_GDU_Z3950 && gdu->u.z3950->which ==
        Z_APDU_initRequest && !f->m_is_multi)
    {
        f->init(package, gdu);
    }
    else if (!f->m_is_multi)
        package.move();
    else if (gdu && gdu->which == Z_GDU_Z3950)
    {
        Z_APDU *apdu = gdu->u.z3950;
        if (apdu->which == Z_APDU_initRequest)
        {
            yp2::odr odr;
            
            package.response() = odr.create_close(
                apdu,
                Z_Close_protocolError,
                "double init");
            
            package.session().close();
        }
        else if (apdu->which == Z_APDU_searchRequest)
        {
            f->search(package, apdu);
        }
        else if (apdu->which == Z_APDU_presentRequest)
        {
            f->present(package, apdu);
        }
        else
        {
            yp2::odr odr;
            
            package.response() = odr.create_close(
                apdu, Z_Close_protocolError,
                "unsupported APDU in filter multi");
            
            package.session().close();
        }
    }
    m_p->release_frontend(package);
}

void yp2::filter::Multi::configure(const xmlNode * ptr)
{
    for (ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        if (!strcmp((const char *) ptr->name, "virtual"))
        {
            std::list<std::string> targets;
            std::string vhost;
            xmlNode *v_node = ptr->children;
            for (; v_node; v_node = v_node->next)
            {
                if (v_node->type != XML_ELEMENT_NODE)
                    continue;
                
                if (yp2::xml::is_element_yp2(v_node, "vhost"))
                    vhost = yp2::xml::get_text(v_node);
                else if (yp2::xml::is_element_yp2(v_node, "target"))
                    targets.push_back(yp2::xml::get_text(v_node));
                else
                    throw yp2::filter::FilterException
                        ("Bad element " 
                         + std::string((const char *) v_node->name)
                         + " in virtual section"
                            );
            }
            std::string route = yp2::xml::get_route(ptr);
            add_map_host2hosts(vhost, targets, route);
            std::list<std::string>::const_iterator it;
            for (it = targets.begin(); it != targets.end(); it++)
            {
                std::cout << "Add " << vhost << "->" << *it
                          << "," << route << "\n";
            }
        }
        else
        {
            throw yp2::filter::FilterException
                ("Bad element " 
                 + std::string((const char *) ptr->name)
                 + " in virt_db filter");
        }
    }
}

static yp2::filter::Base* filter_creator()
{
    return new yp2::filter::Multi;
}

extern "C" {
    struct yp2_filter_struct yp2_filter_multi = {
        0,
        "multi",
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
