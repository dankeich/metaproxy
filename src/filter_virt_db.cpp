/* $Id: filter_virt_db.cpp,v 1.44 2006-08-01 13:24:53 adam Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#include "config.hpp"

#include "filter.hpp"
#include "package.hpp"

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/shared_ptr.hpp>

#include "util.hpp"
#include "filter_virt_db.hpp"

#include <yaz/zgdu.h>
#include <yaz/otherinfo.h>
#include <yaz/diagbib1.h>

#include <map>
#include <iostream>

namespace mp = metaproxy_1;
namespace yf = mp::filter;

namespace metaproxy_1 {
    namespace filter {

        struct Virt_db::Set {
            Set(BackendPtr b, std::string setname);
            Set();
            ~Set();

            BackendPtr m_backend;
            std::string m_setname;
        };
        struct Virt_db::Map {
            Map(std::list<std::string> targets, std::string route);
            Map();
            std::list<std::string> m_targets;
            std::string m_route;
        };
        struct Virt_db::Backend {
            mp::Session m_backend_session;
            std::list<std::string> m_frontend_databases;
            std::list<std::string> m_targets;
            std::string m_route;
            bool m_named_result_sets;
            int m_number_of_sets;
        };
        struct Virt_db::Frontend {
            Frontend(Rep *rep);
            ~Frontend();
            mp::Session m_session;
            bool m_is_virtual;
            bool m_in_use;
            yazpp_1::GDU m_init_gdu;
            std::list<BackendPtr> m_backend_list;
            std::map<std::string,Virt_db::Set> m_sets;

            void search(Package &package, Z_APDU *apdu);
            void present(Package &package, Z_APDU *apdu);
            void scan(Package &package, Z_APDU *apdu);

            void close(Package &package);
            typedef std::map<std::string,Virt_db::Set>::iterator Sets_it;

            void fixup_package(Package &p, BackendPtr b);
            void fixup_npr_record(ODR odr, Z_NamePlusRecord *npr,
                                  BackendPtr b);
            void fixup_npr_records(ODR odr, Z_Records *records,
                                   BackendPtr b);
    
            BackendPtr lookup_backend_from_databases(
                std::list<std::string> databases);
            BackendPtr create_backend_from_databases(
                std::list<std::string> databases,
                int &error_code,
                std::string &failing_database);
            
            BackendPtr init_backend(std::list<std::string> database,
                                    Package &package,
                                    int &error_code, std::string &addinfo);
            Rep *m_p;
        };            
        class Virt_db::Rep {
            friend class Virt_db;
            friend struct Frontend;
            
            FrontendPtr get_frontend(Package &package);
            void release_frontend(Package &package);
        private:
            std::map<std::string, Virt_db::Map>m_maps;
            typedef std::map<std::string,Virt_db::Set>::iterator Sets_it;
            boost::mutex m_mutex;
            boost::condition m_cond_session_ready;
            std::map<mp::Session, FrontendPtr> m_clients;
        };
    }
}

yf::Virt_db::BackendPtr yf::Virt_db::Frontend::lookup_backend_from_databases(
    std::list<std::string> databases)
{
    std::list<BackendPtr>::const_iterator map_it;
    map_it = m_backend_list.begin();
    for (; map_it != m_backend_list.end(); map_it++)
        if ((*map_it)->m_frontend_databases == databases)
            return *map_it;
    BackendPtr null;
    return null;
}

yf::Virt_db::BackendPtr yf::Virt_db::Frontend::create_backend_from_databases(
    std::list<std::string> databases, int &error_code, std::string &addinfo)
{
    BackendPtr b(new Backend);
    std::list<std::string>::const_iterator db_it = databases.begin();

    b->m_number_of_sets = 0;
    b->m_frontend_databases = databases;
    b->m_named_result_sets = false;

    bool first_route = true;

    std::map<std::string,bool> targets_dedup;
    for (; db_it != databases.end(); db_it++)
    {
        std::map<std::string, Virt_db::Map>::iterator map_it;
        map_it = m_p->m_maps.find(mp::util::database_name_normalize(*db_it));
        if (map_it == m_p->m_maps.end())  // database not found
        {
            error_code = YAZ_BIB1_DATABASE_UNAVAILABLE;
            addinfo = *db_it;
            BackendPtr ptr;
            return ptr;
        }
        std::list<std::string>::const_iterator t_it =
            map_it->second.m_targets.begin();
        for (; t_it != map_it->second.m_targets.end(); t_it++)
            targets_dedup[*t_it] = true;

        // see if we have a route conflict.
        if (!first_route && b->m_route != map_it->second.m_route)
        {
            // we have a conflict.. 
            error_code =  YAZ_BIB1_COMBI_OF_SPECIFIED_DATABASES_UNSUPP;
            BackendPtr ptr;
            return ptr;
        }
        b->m_route = map_it->second.m_route;
        first_route = false;
    }
    std::map<std::string,bool>::const_iterator tm_it = targets_dedup.begin();
    for (; tm_it != targets_dedup.end(); tm_it++)
        b->m_targets.push_back(tm_it->first);

    return b;
}

yf::Virt_db::BackendPtr yf::Virt_db::Frontend::init_backend(
    std::list<std::string> databases, mp::Package &package,
    int &error_code, std::string &addinfo)
{
    BackendPtr b = create_backend_from_databases(databases, error_code,
                                                 addinfo);
    if (!b)
        return b;
    Package init_package(b->m_backend_session, package.origin());
    init_package.copy_filter(package);

    mp::odr odr;

    Z_APDU *init_apdu = zget_APDU(odr, Z_APDU_initRequest);

    mp::util::set_vhost_otherinfo(&init_apdu->u.initRequest->otherInfo, odr,
                                   b->m_targets);
    Z_InitRequest *req = init_apdu->u.initRequest;

    // copy stuff from Frontend Init Request
    Z_GDU *org_gdu = m_init_gdu.get();
    Z_InitRequest *org_init = org_gdu->u.z3950->u.initRequest;

    req->idAuthentication = org_init->idAuthentication;
    req->implementationId = org_init->implementationId;
    req->implementationName = org_init->implementationName;
    req->implementationVersion = org_init->implementationVersion;

    ODR_MASK_SET(req->options, Z_Options_search);
    ODR_MASK_SET(req->options, Z_Options_present);
    ODR_MASK_SET(req->options, Z_Options_namedResultSets);
    ODR_MASK_SET(req->options, Z_Options_scan);

    ODR_MASK_SET(req->protocolVersion, Z_ProtocolVersion_1);
    ODR_MASK_SET(req->protocolVersion, Z_ProtocolVersion_2);
    ODR_MASK_SET(req->protocolVersion, Z_ProtocolVersion_3);

    init_package.request() = init_apdu;
    
    init_package.move(b->m_route);  // sending init 

    Z_GDU *gdu = init_package.response().get();
    // we hope to get an init response
    if (gdu && gdu->which == Z_GDU_Z3950 && gdu->u.z3950->which ==
        Z_APDU_initResponse)
    {
        Z_InitResponse *res = gdu->u.z3950->u.initResponse;
        if (ODR_MASK_GET(res->options, Z_Options_namedResultSets))
        {
            b->m_named_result_sets = true;
        }
        if (!*res->result)
        {
            mp::util::get_init_diagnostics(res, error_code, addinfo);
            BackendPtr null;
            return null; 
        }
    }
    else
    {
        error_code = YAZ_BIB1_DATABASE_UNAVAILABLE;
        // addinfo = database;
        BackendPtr null;
        return null;
    }        
    if (init_package.session().is_closed())
    {
        error_code = YAZ_BIB1_DATABASE_UNAVAILABLE;
        // addinfo = database;
        BackendPtr null;
        return null;
    }

    m_backend_list.push_back(b);
    return b;
}

void yf::Virt_db::Frontend::search(mp::Package &package, Z_APDU *apdu_req)
{
    Z_SearchRequest *req = apdu_req->u.searchRequest;
    std::string vhost;
    std::string resultSetId = req->resultSetName;
    mp::odr odr;

    std::list<std::string> databases;
    int i;
    for (i = 0; i<req->num_databaseNames; i++)
        databases.push_back(req->databaseNames[i]);

    BackendPtr b; // null for now
    Sets_it sets_it = m_sets.find(req->resultSetName);
    if (sets_it != m_sets.end())
    {
        // result set already exist 
        // if replace indicator is off: we return diagnostic if
        // result set already exist.
        if (*req->replaceIndicator == 0)
        {
            Z_APDU *apdu = 
                odr.create_searchResponse(
                    apdu_req,
                    YAZ_BIB1_RESULT_SET_EXISTS_AND_REPLACE_INDICATOR_OFF,
                    0);
            package.response() = apdu;
            
            return;
        } 
        sets_it->second.m_backend->m_number_of_sets--;

        // pick up any existing backend with a database match
        std::list<BackendPtr>::const_iterator map_it;
        map_it = m_backend_list.begin();
        for (; map_it != m_backend_list.end(); map_it++)
        {
            BackendPtr tmp = *map_it;
            if (tmp->m_frontend_databases == databases)
                break;
        }
        if (map_it != m_backend_list.end()) 
            b = *map_it;
    }
    else
    {
        // new result set.

        // pick up any existing database with named result sets ..
        // or one which has no result sets.. yet.
        std::list<BackendPtr>::const_iterator map_it;
        map_it = m_backend_list.begin();
        for (; map_it != m_backend_list.end(); map_it++)
        {
            BackendPtr tmp = *map_it;
            if (tmp->m_frontend_databases == databases &&
                (tmp->m_named_result_sets ||
                 tmp->m_number_of_sets == 0))
                break;
        }
        if (map_it != m_backend_list.end()) 
            b = *map_it;
    }
    if (!b)  // no backend yet. Must create a new one
    {
        int error_code;
        std::string addinfo;
        b = init_backend(databases, package, error_code, addinfo);
        if (!b)
        {
            // did not get a backend (unavailable somehow?)
            
            Z_APDU *apdu = 
                odr.create_searchResponse(
                    apdu_req, error_code, addinfo.c_str());
            package.response() = apdu;
            return;
        }
    }
    m_sets.erase(req->resultSetName);
    // sending search to backend
    Package search_package(b->m_backend_session, package.origin());

    search_package.copy_filter(package);

    std::string backend_setname;
    if (b->m_named_result_sets)
    {
        backend_setname = std::string(req->resultSetName);
    }
    else
    {
        backend_setname = "default";
        req->resultSetName = odr_strdup(odr, backend_setname.c_str());
    }

    // pick first targets spec and move the databases from it ..
    std::list<std::string>::const_iterator t_it = b->m_targets.begin();
    if (t_it != b->m_targets.end())
    {
        mp::util::set_databases_from_zurl(odr, *t_it,
                                                &req->num_databaseNames,
                                                &req->databaseNames);
    }

    *req->replaceIndicator = 1;

    search_package.request() = yazpp_1::GDU(apdu_req);
    
    search_package.move(b->m_route);

    if (search_package.session().is_closed())
    {
        package.response() = search_package.response();
        package.session().close();
        return;
    }
    b->m_number_of_sets++;

    m_sets[resultSetId] = Virt_db::Set(b, backend_setname);
    fixup_package(search_package, b);
    package.response() = search_package.response();
}

yf::Virt_db::Frontend::Frontend(Rep *rep)
{
    m_p = rep;
    m_is_virtual = false;
}

void yf::Virt_db::Frontend::close(mp::Package &package)
{
    std::list<BackendPtr>::const_iterator b_it;
    
    for (b_it = m_backend_list.begin(); b_it != m_backend_list.end(); b_it++)
    {
        (*b_it)->m_backend_session.close();
        Package close_package((*b_it)->m_backend_session, package.origin());
        close_package.copy_filter(package);
        close_package.move((*b_it)->m_route);
    }
    m_backend_list.clear();
}

yf::Virt_db::Frontend::~Frontend()
{
}

yf::Virt_db::FrontendPtr yf::Virt_db::Rep::get_frontend(mp::Package &package)
{
    boost::mutex::scoped_lock lock(m_mutex);

    std::map<mp::Session,yf::Virt_db::FrontendPtr>::iterator it;
    
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

void yf::Virt_db::Rep::release_frontend(mp::Package &package)
{
    boost::mutex::scoped_lock lock(m_mutex);
    std::map<mp::Session,yf::Virt_db::FrontendPtr>::iterator it;
    
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

yf::Virt_db::Set::Set(BackendPtr b, std::string setname)
    :  m_backend(b), m_setname(setname)
{
}


yf::Virt_db::Set::Set()
{
}


yf::Virt_db::Set::~Set()
{
}

yf::Virt_db::Map::Map(std::list<std::string> targets, std::string route)
    : m_targets(targets), m_route(route) 
{
}

yf::Virt_db::Map::Map()
{
}

yf::Virt_db::Virt_db() : m_p(new Virt_db::Rep)
{
}

yf::Virt_db::~Virt_db() {
}

void yf::Virt_db::Frontend::fixup_npr_record(ODR odr, Z_NamePlusRecord *npr,
                                             BackendPtr b)
{
    if (npr->databaseName)
    {
        std::string b_database = std::string(npr->databaseName);

        // consider each of the frontend databases..
        std::list<std::string>::const_iterator db_it;
        for (db_it = b->m_frontend_databases.begin(); 
             db_it != b->m_frontend_databases.end(); db_it++)
        {
            // see which target it corresponds to.. (if any)
            std::map<std::string,Virt_db::Map>::const_iterator map_it;
            map_it = m_p->m_maps.find(*db_it);
            if (map_it != m_p->m_maps.end())
            { 
                Virt_db::Map m = map_it->second;
                
                std::list<std::string>::const_iterator t;
                for (t = m.m_targets.begin(); t != m.m_targets.end(); t++)
                {
                    if (*t == b_database)
                    {
                        npr->databaseName = odr_strdup(odr, (*db_it).c_str());
                        return;
                    }
                }
            }
            
        }
        db_it = b->m_frontend_databases.begin();
        if (db_it != b->m_frontend_databases.end())
        {
            std::string database = *db_it;
            npr->databaseName = odr_strdup(odr, database.c_str());
        }
    }
}

void yf::Virt_db::Frontend::fixup_npr_records(ODR odr, Z_Records *records,
                                              BackendPtr b)
{
    if (records && records->which == Z_Records_DBOSD)
    {
        Z_NamePlusRecordList *nprlist = records->u.databaseOrSurDiagnostics;
        int i;
        for (i = 0; i < nprlist->num_records; i++)
        {
            fixup_npr_record(odr, nprlist->records[i], b);
        }
    }
}

void yf::Virt_db::Frontend::fixup_package(mp::Package &p, BackendPtr b)
{
    Z_GDU *gdu = p.response().get();
    mp::odr odr;

    if (gdu && gdu->which == Z_GDU_Z3950)
    {
        Z_APDU *apdu = gdu->u.z3950;
        if (apdu->which == Z_APDU_presentResponse)
        {
            fixup_npr_records(odr, apdu->u.presentResponse->records, b);
            p.response() = gdu;
        }
        else if (apdu->which == Z_APDU_searchResponse)
        {
            fixup_npr_records(odr,  apdu->u.searchResponse->records, b);
            p.response() = gdu;
        }
    }
}

void yf::Virt_db::Frontend::present(mp::Package &package, Z_APDU *apdu_req)
{
    Z_PresentRequest *req = apdu_req->u.presentRequest;
    std::string resultSetId = req->resultSetId;
    mp::odr odr;

    Sets_it sets_it = m_sets.find(resultSetId);
    if (sets_it == m_sets.end())
    {
        Z_APDU *apdu = 
            odr.create_presentResponse(
                apdu_req,
                YAZ_BIB1_SPECIFIED_RESULT_SET_DOES_NOT_EXIST,
                resultSetId.c_str());
        package.response() = apdu;
        return;
    }
    Session *id =
        new mp::Session(sets_it->second.m_backend->m_backend_session);
    
    // sending present to backend
    Package present_package(*id, package.origin());
    present_package.copy_filter(package);

    req->resultSetId = odr_strdup(odr, sets_it->second.m_setname.c_str());
    
    present_package.request() = yazpp_1::GDU(apdu_req);

    present_package.move(sets_it->second.m_backend->m_route);

    fixup_package(present_package, sets_it->second.m_backend);

    if (present_package.session().is_closed())
    {
        package.response() = present_package.response();
        package.session().close();
        return;
    }
    else
    {
        package.response() = present_package.response();
    }
    delete id;
}

void yf::Virt_db::Frontend::scan(mp::Package &package, Z_APDU *apdu_req)
{
    Z_ScanRequest *req = apdu_req->u.scanRequest;
    std::string vhost;
    mp::odr odr;

    std::list<std::string> databases;
    int i;
    for (i = 0; i<req->num_databaseNames; i++)
        databases.push_back(req->databaseNames[i]);

    BackendPtr b;
    // pick up any existing backend with a database match
    std::list<BackendPtr>::const_iterator map_it;
    map_it = m_backend_list.begin();
    for (; map_it != m_backend_list.end(); map_it++)
    {
        BackendPtr tmp = *map_it;
        if (tmp->m_frontend_databases == databases)
            break;
    }
    if (map_it != m_backend_list.end()) 
        b = *map_it;
    if (!b)  // no backend yet. Must create a new one
    {
        int error_code;
        std::string addinfo;
        b = init_backend(databases, package, error_code, addinfo);
        if (!b)
        {
            // did not get a backend (unavailable somehow?)
            Z_APDU *apdu =
                odr.create_scanResponse(
                    apdu_req, error_code, addinfo.c_str());
            package.response() = apdu;
            
            return;
        }
    }
    // sending scan to backend
    Package scan_package(b->m_backend_session, package.origin());

    scan_package.copy_filter(package);

    // pick first targets spec and move the databases from it ..
    std::list<std::string>::const_iterator t_it = b->m_targets.begin();
    if (t_it != b->m_targets.end())
    {
        mp::util::set_databases_from_zurl(odr, *t_it,
                                                &req->num_databaseNames,
                                                &req->databaseNames);
    }
    scan_package.request() = yazpp_1::GDU(apdu_req);
    
    scan_package.move(b->m_route);

    if (scan_package.session().is_closed())
    {
        package.response() = scan_package.response();
        package.session().close();
        return;
    }
    package.response() = scan_package.response();
}


void yf::Virt_db::add_map_db2targets(std::string db, 
                                     std::list<std::string> targets,
                                     std::string route)
{
    m_p->m_maps[mp::util::database_name_normalize(db)] 
        = Virt_db::Map(targets, route);
}


void yf::Virt_db::add_map_db2target(std::string db, 
                                    std::string target,
                                    std::string route)
{
    std::list<std::string> targets;
    targets.push_back(target);

    m_p->m_maps[mp::util::database_name_normalize(db)]
        = Virt_db::Map(targets, route);
}

void yf::Virt_db::process(mp::Package &package) const
{
    FrontendPtr f = m_p->get_frontend(package);

    Z_GDU *gdu = package.request().get();
    
    if (gdu && gdu->which == Z_GDU_Z3950 && gdu->u.z3950->which ==
        Z_APDU_initRequest && !f->m_is_virtual)
    {
        Z_InitRequest *req = gdu->u.z3950->u.initRequest;
        
        std::list<std::string> vhosts;
        mp::util::get_vhost_otherinfo(&req->otherInfo, false, vhosts);
        if (vhosts.size() == 0)
        {
            f->m_init_gdu = gdu;
            
            mp::odr odr;
            Z_APDU *apdu = odr.create_initResponse(gdu->u.z3950, 0, 0);
            Z_InitResponse *resp = apdu->u.initResponse;
            
            int i;
            static const int masks[] = {
                Z_Options_search,
                Z_Options_present,
                Z_Options_namedResultSets,
                Z_Options_scan,
                -1 
            };
            for (i = 0; masks[i] != -1; i++)
                if (ODR_MASK_GET(req->options, masks[i]))
                    ODR_MASK_SET(resp->options, masks[i]);
            
            static const int versions[] = {
                Z_ProtocolVersion_1,
                Z_ProtocolVersion_2,
                Z_ProtocolVersion_3,
                -1
            };
            for (i = 0; versions[i] != -1; i++)
                if (ODR_MASK_GET(req->protocolVersion, versions[i]))
                    ODR_MASK_SET(resp->protocolVersion, versions[i]);
                else
                    break;
            
            package.response() = apdu;
            f->m_is_virtual = true;
        }
        else
            package.move();
    }
    else if (!f->m_is_virtual)
        package.move();
    else if (gdu && gdu->which == Z_GDU_Z3950)
    {
        Z_APDU *apdu = gdu->u.z3950;
        if (apdu->which == Z_APDU_initRequest)
        {
            mp::odr odr;
            
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
        else if (apdu->which == Z_APDU_scanRequest)
        {
            f->scan(package, apdu);
        }
        else if (apdu->which == Z_APDU_close)
        {
            package.session().close();
        }
        else
        {
            mp::odr odr;
            
            package.response() = odr.create_close(
                apdu, Z_Close_protocolError,
                "unsupported APDU in filter_virt_db");
            
            package.session().close();
        }
    }
    m_p->release_frontend(package);
}


void mp::filter::Virt_db::configure(const xmlNode * ptr)
{
    for (ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        if (!strcmp((const char *) ptr->name, "virtual"))
        {
            std::string database;
            std::list<std::string> targets;
            xmlNode *v_node = ptr->children;
            for (; v_node; v_node = v_node->next)
            {
                if (v_node->type != XML_ELEMENT_NODE)
                    continue;
                
                if (mp::xml::is_element_yp2(v_node, "database"))
                    database = mp::xml::get_text(v_node);
                else if (mp::xml::is_element_yp2(v_node, "target"))
                    targets.push_back(mp::xml::get_text(v_node));
                else
                    throw mp::filter::FilterException
                        ("Bad element " 
                         + std::string((const char *) v_node->name)
                         + " in virtual section"
                            );
            }
            std::string route = mp::xml::get_route(ptr);
            add_map_db2targets(database, targets, route);
        }
        else
        {
            throw mp::filter::FilterException
                ("Bad element " 
                 + std::string((const char *) ptr->name)
                 + " in virt_db filter");
        }
    }
}

static mp::filter::Base* filter_creator()
{
    return new mp::filter::Virt_db;
}

extern "C" {
    struct metaproxy_1_filter_struct metaproxy_1_filter_virt_db = {
        0,
        "virt_db",
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
