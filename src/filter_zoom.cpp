/* This file is part of Metaproxy.
   Copyright (C) 2005-2011 Index Data

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

#include <stdlib.h>
#include <sys/types.h>
#include "filter_zoom.hpp"
#include <yaz/zoom.h>
#include <yaz/yaz-version.h>
#include <yaz/tpath.h>
#include <yaz/srw.h>
#include <metaproxy/package.hpp>
#include <metaproxy/util.hpp>
#include <metaproxy/xmlutil.hpp>
#include "torus.hpp"

#include <libxslt/xsltutils.h>
#include <libxslt/transform.h>

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <yaz/ccl_xml.h>
#include <yaz/ccl.h>
#include <yaz/rpn2cql.h>
#include <yaz/rpn2solr.h>
#include <yaz/pquery.h>
#include <yaz/cql.h>
#include <yaz/oid_db.h>
#include <yaz/diagbib1.h>
#include <yaz/log.h>
#include <yaz/zgdu.h>
#include <yaz/querytowrbuf.h>

namespace mp = metaproxy_1;
namespace yf = mp::filter;

namespace metaproxy_1 {
    namespace filter {
        class Zoom::Searchable : boost::noncopyable {
          public:
            std::string authentication;
            std::string cfAuth;
            std::string cfProxy;
            std::string cfSubDb;
            std::string udb;
            std::string target;
            std::string query_encoding;
            std::string sru;
            std::string request_syntax;
            std::string element_set;
            std::string record_encoding;
            std::string transform_xsl_fname;
            std::string urlRecipe;
            std::string contentConnector;
            bool use_turbomarc;
            bool piggyback;
            CCL_bibset ccl_bibset;
            Searchable(CCL_bibset base);
            ~Searchable();
        };
        class Zoom::Backend : boost::noncopyable {
            friend class Impl;
            friend class Frontend;
            std::string zurl;
            ZOOM_connection m_connection;
            ZOOM_resultset m_resultset;
            std::string m_frontend_database;
            SearchablePtr sptr;
            xsltStylesheetPtr xsp;
            std::string content_session_id;
        public:
            Backend(SearchablePtr sptr);
            ~Backend();
            void connect(std::string zurl, int *error, char **addinfo,
                         ODR odr);
            void search_pqf(const char *pqf, Odr_int *hits,
                            int *error, char **addinfo, ODR odr);
            void search_cql(const char *cql, Odr_int *hits,
                            int *error, char **addinfo, ODR odr);
            void present(Odr_int start, Odr_int number, ZOOM_record *recs,
                         int *error, char **addinfo, ODR odr);
            void set_option(const char *name, const char *value);
            void set_option(const char *name, std::string value);
            const char *get_option(const char *name);
            void get_zoom_error(int *error, char **addinfo, ODR odr);
        };
        class Zoom::Frontend : boost::noncopyable {
            friend class Impl;
            Impl *m_p;
            bool m_is_virtual;
            bool m_in_use;
            yazpp_1::GDU m_init_gdu;
            BackendPtr m_backend;
            void handle_package(mp::Package &package);
            void handle_search(mp::Package &package);
            void handle_present(mp::Package &package);
            BackendPtr get_backend_from_databases(std::string &database,
                                                  int *error,
                                                  char **addinfo,
                                                  ODR odr);


            void prepare_elements(BackendPtr b,
                                  Odr_oid *preferredRecordSyntax,
                                  const char *element_set_name,
                                  bool &enable_pz2_retrieval,
                                  bool &enable_pz2_transform,
                                  bool &assume_marc8_charset);

            Z_Records *get_records(Odr_int start,
                                   Odr_int number_to_present,
                                   int *error,
                                   char **addinfo,
                                   Odr_int *number_of_records_returned,
                                   ODR odr, BackendPtr b,
                                   Odr_oid *preferredRecordSyntax,
                                   const char *element_set_name);
        public:
            Frontend(Impl *impl);
            ~Frontend();
        };
        class Zoom::Impl {
            friend class Frontend;
        public:
            Impl();
            ~Impl();
            void process(metaproxy_1::Package & package);
            void configure(const xmlNode * ptr, bool test_only,
                           const char *path);
        private:
            void configure_local_records(const xmlNode * ptr, bool test_only);
            FrontendPtr get_frontend(mp::Package &package);
            void release_frontend(mp::Package &package);
            SearchablePtr parse_torus_record(const xmlNode *ptr);
            struct cql_node *convert_cql_fields(struct cql_node *cn, ODR odr);
            std::map<mp::Session, FrontendPtr> m_clients;            
            boost::mutex m_mutex;
            boost::condition m_cond_session_ready;
            std::string torus_url;
            std::map<std::string,std::string> fieldmap;
            std::string xsldir;
            std::string file_path;
            std::string content_proxy_server;
            std::string content_tmp_file;
            bool apdu_log;
            CCL_bibset bibset;
            std::string element_transform;
            std::string element_raw;
            std::string proxy;
            std::map<std::string,SearchablePtr> s_map;
        };
    }
}

// define Pimpl wrapper forwarding to Impl
 
yf::Zoom::Zoom() : m_p(new Impl)
{
}

yf::Zoom::~Zoom()
{  // must have a destructor because of boost::scoped_ptr
}

void yf::Zoom::configure(const xmlNode *xmlnode, bool test_only,
                         const char *path)
{
    m_p->configure(xmlnode, test_only, path);
}

void yf::Zoom::process(mp::Package &package) const
{
    m_p->process(package);
}


// define Implementation stuff

yf::Zoom::Backend::Backend(SearchablePtr ptr) : sptr(ptr)
{
    m_connection = ZOOM_connection_create(0);
    m_resultset = 0;
    xsp = 0;
}

yf::Zoom::Backend::~Backend()
{
    if (xsp)
        xsltFreeStylesheet(xsp);
    ZOOM_connection_destroy(m_connection);
    ZOOM_resultset_destroy(m_resultset);
}


void yf::Zoom::Backend::get_zoom_error(int *error, char **addinfo,
                                       ODR odr)
{
    const char *msg = 0;
    const char *zoom_addinfo = 0;
    const char *dset = 0;
    *error = ZOOM_connection_error_x(m_connection, &msg, &zoom_addinfo, &dset);
    if (*error)
    {
        if (*error >= ZOOM_ERROR_CONNECT)
        {
            // turn ZOOM diagnostic into a Bib-1 2: with addinfo=zoom err msg
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = (char *) odr_malloc(
                odr, 20 + strlen(msg) + 
                (zoom_addinfo ? strlen(zoom_addinfo) : 0));
            strcpy(*addinfo, msg);
            if (zoom_addinfo)
            {
                strcat(*addinfo, ": ");
                strcat(*addinfo, zoom_addinfo);
                strcat(*addinfo, " ");
            }
        }
        else
        {
            if (dset && !strcmp(dset, "info:srw/diagnostic/1"))
                *error = yaz_diag_srw_to_bib1(*error);
            *addinfo = (char *) odr_malloc(
                odr, 20 + (zoom_addinfo ? strlen(zoom_addinfo) : 0));
            **addinfo = '\0';
            if (zoom_addinfo && *zoom_addinfo)
            {
                strcpy(*addinfo, zoom_addinfo);
                strcat(*addinfo, " ");
            }
            strcat(*addinfo, "(backend)");
        }
    }
}

void yf::Zoom::Backend::connect(std::string zurl,
                                int *error, char **addinfo,
                                ODR odr)
{
    ZOOM_connection_connect(m_connection, zurl.c_str(), 0);
    get_zoom_error(error, addinfo, odr);
}

void yf::Zoom::Backend::search_pqf(const char *pqf, Odr_int *hits,
                                   int *error, char **addinfo, ODR odr)
{
    m_resultset = ZOOM_connection_search_pqf(m_connection, pqf);
    get_zoom_error(error, addinfo, odr);
    if (*error == 0)
        *hits = ZOOM_resultset_size(m_resultset);
    else
        *hits = 0;
}

void yf::Zoom::Backend::search_cql(const char *cql, Odr_int *hits,
                                   int *error, char **addinfo, ODR odr)
{
    ZOOM_query q = ZOOM_query_create();

    ZOOM_query_cql(q, cql);

    m_resultset = ZOOM_connection_search(m_connection, q);
    ZOOM_query_destroy(q);
    get_zoom_error(error, addinfo, odr);
    if (*error == 0)
        *hits = ZOOM_resultset_size(m_resultset);
    else
        *hits = 0;
}

void yf::Zoom::Backend::present(Odr_int start, Odr_int number,
                                ZOOM_record *recs,
                                int *error, char **addinfo, ODR odr)
{
    ZOOM_resultset_records(m_resultset, recs, start, number);
    get_zoom_error(error, addinfo, odr);
}

void yf::Zoom::Backend::set_option(const char *name, const char *value)
{
    ZOOM_connection_option_set(m_connection, name, value);
    if (m_resultset)
        ZOOM_resultset_option_set(m_resultset, name, value);
}

void yf::Zoom::Backend::set_option(const char *name, std::string value)
{
    set_option(name, value.c_str());
}

const char *yf::Zoom::Backend::get_option(const char *name)
{
    return ZOOM_connection_option_get(m_connection, name);
}

yf::Zoom::Searchable::Searchable(CCL_bibset base)
{
    piggyback = true;
    use_turbomarc = true;
    ccl_bibset = ccl_qual_dup(base);
}

yf::Zoom::Searchable::~Searchable()
{
    ccl_qual_rm(&ccl_bibset);
}

yf::Zoom::Frontend::Frontend(Impl *impl) : 
    m_p(impl), m_is_virtual(false), m_in_use(true)
{
}

yf::Zoom::Frontend::~Frontend()
{
}

yf::Zoom::FrontendPtr yf::Zoom::Impl::get_frontend(mp::Package &package)
{
    boost::mutex::scoped_lock lock(m_mutex);

    std::map<mp::Session,yf::Zoom::FrontendPtr>::iterator it;
    
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

void yf::Zoom::Impl::release_frontend(mp::Package &package)
{
    boost::mutex::scoped_lock lock(m_mutex);
    std::map<mp::Session,yf::Zoom::FrontendPtr>::iterator it;
    
    it = m_clients.find(package.session());
    if (it != m_clients.end())
    {
        if (package.session().is_closed())
        {
            m_clients.erase(it);
        }
        else
        {
            it->second->m_in_use = false;
        }
        m_cond_session_ready.notify_all();
    }
}

yf::Zoom::Impl::Impl() :
    apdu_log(false), element_transform("pz2") , element_raw("raw")
{
    bibset = ccl_qual_mk();

    srand((unsigned int) time(0));
}

yf::Zoom::Impl::~Impl()
{ 
    ccl_qual_rm(&bibset);
}

yf::Zoom::SearchablePtr yf::Zoom::Impl::parse_torus_record(const xmlNode *ptr)
{
    Zoom::SearchablePtr s(new Searchable(bibset));
    
    for (ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        if (!strcmp((const char *) ptr->name, "layer"))
            ptr = ptr->children;
        else if (!strcmp((const char *) ptr->name,
                         "authentication"))
        {
            s->authentication = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "cfAuth"))
        {
            s->cfAuth = mp::xml::get_text(ptr);
        } 
        else if (!strcmp((const char *) ptr->name,
                         "cfProxy"))
        {
            s->cfProxy = mp::xml::get_text(ptr);
        }  
        else if (!strcmp((const char *) ptr->name,
                         "cfSubDb"))
        {
            s->cfSubDb = mp::xml::get_text(ptr);
        }  
        else if (!strcmp((const char *) ptr->name,
                         "contentConnector"))
        {
            s->contentConnector = mp::xml::get_text(ptr);
        }  
        else if (!strcmp((const char *) ptr->name, "udb"))
        {
            s->udb = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name, "zurl"))
        {
            s->target = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name, "sru"))
        {
            s->sru = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "queryEncoding"))
        {
            s->query_encoding = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "piggyback"))
        {
            s->piggyback = mp::xml::get_bool(ptr, true);
        }
        else if (!strcmp((const char *) ptr->name,
                         "requestSyntax"))
        {
            s->request_syntax = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "elementSet"))
        {
            s->element_set = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "recordEncoding"))
        {
            s->record_encoding = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "transform"))
        {
            s->transform_xsl_fname = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "urlRecipe"))
        {
            s->urlRecipe = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "useTurboMarc"))
        {
            ; // useTurboMarc is ignored
        }
        else if (!strncmp((const char *) ptr->name,
                          "cclmap_", 7))
        {
            std::string value = mp::xml::get_text(ptr);
            ccl_qual_fitem(s->ccl_bibset, value.c_str(),
                           (const char *) ptr->name + 7);
        }
    }
    return s;
}

void yf::Zoom::Impl::configure_local_records(const xmlNode *ptr, bool test_only)
{
    while (ptr && ptr->type != XML_ELEMENT_NODE)
        ptr = ptr->next;
    
    if (ptr)
    {
        if (!strcmp((const char *) ptr->name, "records"))
        {
            for (ptr = ptr->children; ptr; ptr = ptr->next)
            {
                if (ptr->type != XML_ELEMENT_NODE)
                    continue;
                if (!strcmp((const char *) ptr->name, "record"))
                {
                    SearchablePtr s = parse_torus_record(ptr);
                    if (s)
                    {
                        std::string udb = s->udb;
                        if (udb.length())
                            s_map[s->udb] = s;
                        else
                        {
                            throw mp::filter::FilterException
                                ("No udb for local torus record");
                        }
                    }
                }
                else
                {
                    throw mp::filter::FilterException
                        ("Bad element " 
                         + std::string((const char *) ptr->name)
                         + " in zoom filter inside element "
                         "<torus><records>");
                }
            }
        }
        else
        {
            throw mp::filter::FilterException
                ("Bad element " 
                 + std::string((const char *) ptr->name)
                 + " in zoom filter inside element <torus>");
        }
    }
}

void yf::Zoom::Impl::configure(const xmlNode *ptr, bool test_only,
                               const char *path)
{
    content_tmp_file = "/tmp/cf.XXXXXX.p";
    if (path && *path)
    {
        file_path = path;
    }
    for (ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        else if (!strcmp((const char *) ptr->name, "torus"))
        {
            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "url"))
                    torus_url = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "xsldir"))
                    xsldir = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "element_transform"))
                    element_transform = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "element_raw"))
                    element_raw = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "proxy"))
                    proxy = mp::xml::get_text(attr->children);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
            configure_local_records(ptr->children, test_only);
        }
        else if (!strcmp((const char *) ptr->name, "cclmap"))
        {
            const char *addinfo = 0;
            ccl_xml_config(bibset, ptr, &addinfo);
        }
        else if (!strcmp((const char *) ptr->name, "fieldmap"))
        {
            const struct _xmlAttr *attr;
            std::string ccl_field;
            std::string cql_field;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "ccl"))
                    ccl_field = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "cql"))
                    cql_field = mp::xml::get_text(attr->children);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
            if (cql_field.length())
                fieldmap[cql_field] = ccl_field;
        }
        else if (!strcmp((const char *) ptr->name, "contentProxy"))
        {
            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "server"))
                    content_proxy_server = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "tmp_file"))
                    content_tmp_file = mp::xml::get_text(attr->children);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
        }
        else if (!strcmp((const char *) ptr->name, "log"))
        { 
            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "apdu"))
                    apdu_log = mp::xml::get_bool(attr->children, false);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
        }
        else
        {
            throw mp::filter::FilterException
                ("Bad element " 
                 + std::string((const char *) ptr->name)
                 + " in zoom filter");
        }
    }
}

yf::Zoom::BackendPtr yf::Zoom::Frontend::get_backend_from_databases(
    std::string &database, int *error, char **addinfo, ODR odr)
{
    std::list<BackendPtr>::const_iterator map_it;
    if (m_backend && m_backend->m_frontend_database == database)
        return m_backend;

    std::string db_args;
    std::string torus_db;
    size_t db_arg_pos = database.find(',');
    if (db_arg_pos != std::string::npos)
    {
        torus_db = database.substr(0, db_arg_pos);
        db_args = database.substr(db_arg_pos + 1);
    }
    else
        torus_db = database;
 
    SearchablePtr sptr;

    std::map<std::string,SearchablePtr>::iterator it;
    it = m_p->s_map.find(torus_db);
    if (it != m_p->s_map.end())
        sptr = it->second;
    else
    {
        xmlDoc *doc = mp::get_searchable(m_p->torus_url, torus_db, m_p->proxy);
        if (!doc)
        {
            *error = YAZ_BIB1_DATABASE_DOES_NOT_EXIST;
            *addinfo = odr_strdup(odr, database.c_str());
            BackendPtr b;
            return b;
        }
        const xmlNode *ptr = xmlDocGetRootElement(doc);
        if (ptr)
        {   // presumably ptr is a records element node
            // parse first record in document
            for (ptr = ptr->children; ptr; ptr = ptr->next)
            {
                if (ptr->type == XML_ELEMENT_NODE
                    && !strcmp((const char *) ptr->name, "record"))
                {
                    if (sptr)
                    {
                        *error = YAZ_BIB1_UNSPECIFIED_ERROR;
                        *addinfo = (char*) odr_malloc(odr, 40 + database.length()),
                        sprintf(*addinfo, "multiple records for udb=%s",
                                 database.c_str());
                        xmlFreeDoc(doc);
                        BackendPtr b;
                        return b;
                    }
                    sptr = m_p->parse_torus_record(ptr);
                }
            }
        }
        xmlFreeDoc(doc);
    }

    if (!sptr)
    {
        *error = YAZ_BIB1_DATABASE_DOES_NOT_EXIST;
        *addinfo = odr_strdup(odr, database.c_str());
        BackendPtr b;
        return b;
    }
        
    xsltStylesheetPtr xsp = 0;
    if (sptr->transform_xsl_fname.length())
    {
        const char *path = 0;

        if (m_p->xsldir.length())
            path = m_p->xsldir.c_str();
        else
            path = m_p->file_path.c_str();
        std::string fname;

        char fullpath[1024];
        char *cp = yaz_filepath_resolve(sptr->transform_xsl_fname.c_str(),
                                        path, 0, fullpath);
        if (cp)
            fname.assign(cp);
        else
        {
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = (char *)
                odr_malloc(odr, 40 + sptr->transform_xsl_fname.length());
            sprintf(*addinfo, "File could not be read: %s", 
                    sptr->transform_xsl_fname.c_str());
            BackendPtr b;
            return b;
        }
        xmlDoc *xsp_doc = xmlParseFile(fname.c_str());
        if (!xsp_doc)
        {
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = (char *) odr_malloc(odr, 40 + fname.length());
            sprintf(*addinfo, "xmlParseFile failed. File: %s", fname.c_str());
            BackendPtr b;
            return b;
        }
        xsp = xsltParseStylesheetDoc(xsp_doc);
        if (!xsp)
        {
            *error = YAZ_BIB1_DATABASE_DOES_NOT_EXIST;
            *addinfo = odr_strdup(odr, "xsltParseStylesheetDoc failed");
            BackendPtr b;
            xmlFreeDoc(xsp_doc);
            return b;
        }
    }

    m_backend.reset();

    BackendPtr b(new Backend(sptr));

    b->xsp = xsp;
    b->m_frontend_database = database;

    if (sptr->query_encoding.length())
        b->set_option("rpnCharset", sptr->query_encoding);

    b->set_option("timeout", "40");
    
    if (m_p->apdu_log) 
        b->set_option("apdulog", "1");

    if (sptr->piggyback)
        b->set_option("count", "1"); /* some SRU servers INSIST on getting
                                        maximumRecords > 0 */
    b->set_option("piggyback", sptr->piggyback ? "1" : "0");

    std::string authentication = sptr->authentication;
    std::string proxy = sptr->cfProxy;
        
    const char *param_user = 0;
    const char *param_password = 0;
    const char *param_proxy = 0;
    if (db_args.length())
    {
        char **names;
        char **values;
        int i;
        int no_parms = yaz_uri_to_array(db_args.c_str(),
                                        odr, &names, &values);
        for (i = 0; i < no_parms; i++)
        {
            const char *name = names[i];
            const char *value = values[i];
            if (!strcmp(name, "user"))
                param_user = value;
            else if (!strcmp(name, "password"))
                param_password = value;
            else if (!strcmp(name, "proxy"))
                param_proxy = value;
            else if (!strcmp(name, "cproxysession"))
                ;
            else
            {
                BackendPtr notfound;
                char *msg = (char*) odr_malloc(odr, strlen(name) + 30);
                *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
                sprintf(msg, "Bad database argument: %s", name);
                *addinfo = msg;
                return notfound;
            }
        }
        if (param_user)
        {
            authentication = std::string(param_user);
            if (param_password)
                authentication += "/" + std::string(param_password);
        }
        if (param_proxy)
            proxy = param_proxy;
    }

    if (sptr->cfAuth.length())
    {
        // A CF target
        b->set_option("user", sptr->cfAuth);
        if (!param_user && !param_password && authentication.length())
        {
            if (db_args.length())
                db_args += "&";
            // no database (auth) args specified already.. and the
            // Torus authentication has it.. Generate the args that CF
            // understands..
            size_t found = authentication.find('/');
            if (found != std::string::npos)
            {
                db_args += "user=" +
                    mp::util::uri_encode(authentication.substr(0, found))
                    + "&password=" +
                    mp::util::uri_encode(authentication.substr(found+1));
            }
            else
                db_args += "user=" + mp::util::uri_encode(authentication);
        }
        if (!param_proxy && proxy.length())
        {
            if (db_args.length())
                db_args += "&";
            db_args += "proxy=" + mp::util::uri_encode(proxy);
        }
        if (sptr->cfSubDb.length())
        {
            if (db_args.length())
                db_args += "&";
            db_args += "subdatabase=" + mp::util::uri_encode(sptr->cfSubDb);
        }
    }
    else
    {
        db_args.clear(); // no arguments to be passed (non-CF)

        size_t found = authentication.find('/');
        
        if (sptr->sru.length() && found != std::string::npos)
        {
            b->set_option("user", authentication.substr(0, found));
            b->set_option("password", authentication.substr(found+1));
        }
        else
            b->set_option("user", authentication);

        if (proxy.length())
            b->set_option("proxy", proxy);
    }
    if (b->sptr->contentConnector.length())
    {
        char *fname = (char *) xmalloc(m_p->content_tmp_file.length() + 8);
        strcpy(fname, m_p->content_tmp_file.c_str());
        char *xx = strstr(fname, "XXXXXX");
        if (!xx)
        {
            xx = fname + strlen(fname);
            strcat(fname, "XXXXXX");
        }
        char tmp_char = xx[6];
        sprintf(xx, "%06d", ((unsigned) rand()) % 1000000);
        xx[6] = tmp_char;

        FILE *file = fopen(fname, "w");
        if (!file)
        {
            yaz_log(YLOG_WARN|YLOG_ERRNO, "create %s", fname);
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = (char *)  odr_malloc(odr, 40 + strlen(fname));
            sprintf(*addinfo, "Could not create %s", fname);
            xfree(fname);
            BackendPtr backend_null;
            return backend_null;
        }
        b->content_session_id.assign(xx, 6);
        WRBUF w = wrbuf_alloc();
        wrbuf_puts(w, "#content_proxy\n");
        wrbuf_printf(w, "connector: %s\n", b->sptr->contentConnector.c_str());
        if (authentication.length())
            wrbuf_printf(w, "authentication: %s\n", authentication.c_str());
        if (proxy.length())
            wrbuf_printf(w, "proxy: %s\n", proxy.c_str());
        if (sptr->cfProxy.length())
            wrbuf_printf(w, "cfproxy: %s\n", sptr->cfProxy.c_str());

        fwrite(wrbuf_buf(w), 1, wrbuf_len(w), file);
        fclose(file);
        yaz_log(YLOG_LOG, "file %s created\n", fname);
        xfree(fname);
    }

    std::string url;
    if (sptr->sru.length())
    {
        url = "http://" + sptr->target;
        b->set_option("sru", sptr->sru);
    }
    else
    {
        url = sptr->target;
    }
    if (db_args.length())
        url += "," + db_args;
    yaz_log(YLOG_LOG, "url=%s", url.c_str());
    b->connect(url, error, addinfo, odr);
    if (*error == 0)
    {
        m_backend = b;
    }
    return b;
}

void yf::Zoom::Frontend::prepare_elements(BackendPtr b,
                                          Odr_oid *preferredRecordSyntax,
                                          const char *element_set_name,
                                          bool &enable_pz2_retrieval,
                                          bool &enable_pz2_transform,
                                          bool &assume_marc8_charset)

{
    char oid_name_str[OID_STR_MAX];
    const char *syntax_name = 0;
    
    if (preferredRecordSyntax &&
        !oid_oidcmp(preferredRecordSyntax, yaz_oid_recsyn_xml)
        && element_set_name)
    {
        if (!strcmp(element_set_name, m_p->element_transform.c_str()))
        {
            enable_pz2_retrieval = true;
            enable_pz2_transform = true;
        }
        else if (!strcmp(element_set_name, m_p->element_raw.c_str()))
        {
            enable_pz2_retrieval = true;
        }
    }
    
    if (enable_pz2_retrieval)
    {
        std::string configured_request_syntax = b->sptr->request_syntax;
        if (configured_request_syntax.length())
        {
            syntax_name = configured_request_syntax.c_str();
            const Odr_oid *syntax_oid = 
                yaz_string_to_oid(yaz_oid_std(), CLASS_RECSYN, syntax_name);
            if (!oid_oidcmp(syntax_oid, yaz_oid_recsyn_usmarc)
                || !oid_oidcmp(syntax_oid, yaz_oid_recsyn_opac))
                assume_marc8_charset = true;
        }
    }
    else if (preferredRecordSyntax)
        syntax_name =
            yaz_oid_to_string_buf(preferredRecordSyntax, 0, oid_name_str);

    if (b->sptr->sru.length())
        syntax_name = "XML";

    b->set_option("preferredRecordSyntax", syntax_name);

    if (enable_pz2_retrieval)
    {
        element_set_name = 0;
        if (b->sptr->element_set.length())
            element_set_name = b->sptr->element_set.c_str();
    }

    b->set_option("elementSetName", element_set_name);

}

Z_Records *yf::Zoom::Frontend::get_records(Odr_int start,
                                           Odr_int number_to_present,
                                           int *error,
                                           char **addinfo,
                                           Odr_int *number_of_records_returned,
                                           ODR odr,
                                           BackendPtr b,
                                           Odr_oid *preferredRecordSyntax,
                                           const char *element_set_name)
{
    *number_of_records_returned = 0;
    Z_Records *records = 0;
    bool enable_pz2_retrieval = false; // whether target profile is used
    bool enable_pz2_transform = false; // whether XSLT is used as well
    bool assume_marc8_charset = false;

    prepare_elements(b, preferredRecordSyntax,
                     element_set_name,
                     enable_pz2_retrieval,
                     enable_pz2_transform,
                     assume_marc8_charset);

    if (start < 0 || number_to_present <=0)
        return records;
    
    if (number_to_present > 10000)
        number_to_present = 10000;

    ZOOM_record *recs = (ZOOM_record *)
        odr_malloc(odr, (size_t) number_to_present * sizeof(*recs));

    b->present(start, number_to_present, recs, error, addinfo, odr);

    int i = 0;
    if (!*error)
    {
        for (i = 0; i < number_to_present; i++)
            if (!recs[i])
                break;
    }
    if (i > 0)
    {  // only return records if no error and at least one record
        char *odr_database = odr_strdup(odr,
                                        b->m_frontend_database.c_str());
        Z_NamePlusRecordList *npl = (Z_NamePlusRecordList *)
            odr_malloc(odr, sizeof(*npl));
        *number_of_records_returned = i;
        npl->num_records = i;
        npl->records = (Z_NamePlusRecord **)
            odr_malloc(odr, i * sizeof(*npl->records));
        for (i = 0; i < number_to_present; i++)
        {
            Z_NamePlusRecord *npr = 0;
            const char *addinfo;
            int sur_error = ZOOM_record_error(recs[i], 0 /* msg */,
                                              &addinfo, 0 /* diagset */);
                
            if (sur_error)
            {
                npr = zget_surrogateDiagRec(odr, odr_database, sur_error,
                                            addinfo);
            }
            else if (enable_pz2_retrieval)
            {
                char rec_type_str[100];
                const char *record_encoding = 0;

                if (b->sptr->record_encoding.length())
                    record_encoding = b->sptr->record_encoding.c_str();
                else if (assume_marc8_charset)
                    record_encoding = "marc8";

                strcpy(rec_type_str, b->sptr->use_turbomarc ? "txml" : "xml");
                if (record_encoding)
                {
                    strcat(rec_type_str, "; charset=");
                    strcat(rec_type_str, record_encoding);
                }
                
                int rec_len;
                xmlChar *xmlrec_buf = 0;
                const char *rec_buf = ZOOM_record_get(recs[i], rec_type_str,
                                                      &rec_len);
                if (!rec_buf && !npr)
                {
                    std::string addinfo("ZOOM_record_get failed for type ");

                    addinfo += rec_type_str;
                    npr = zget_surrogateDiagRec(
                        odr, odr_database, 
                        YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS,
                        addinfo.c_str());
                }

                if (rec_buf && b->xsp && enable_pz2_transform)
                {
                    xmlDoc *rec_doc = xmlParseMemory(rec_buf, rec_len);
                    if (!rec_doc)
                    {
                        npr = zget_surrogateDiagRec(
                            odr, odr_database, 
                            YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS,
                            "xml parse failed for record");
                    }
                    else
                    { 
                        xmlDoc *rec_res = 
                            xsltApplyStylesheet(b->xsp, rec_doc, 0);

                        if (rec_res)
                        {
                            xsltSaveResultToString(&xmlrec_buf, &rec_len,
                                                   rec_res, b->xsp);
                            rec_buf = (const char *) xmlrec_buf;

                            xmlFreeDoc(rec_res);
                        }
                        if (!rec_buf)
                        {
                            std::string addinfo;

                            addinfo = "xslt apply failed for "
                                + b->sptr->transform_xsl_fname;
                            npr = zget_surrogateDiagRec(
                                odr, odr_database, 
                                YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS,
                                addinfo.c_str());
                        }
                        xmlFreeDoc(rec_doc);
                    }
                }

                if (rec_buf)
                {
                    xmlDoc *doc = xmlParseMemory(rec_buf, rec_len);
                    std::string res = 
                        mp::xml::url_recipe_handle(doc, b->sptr->urlRecipe);
                    if (res.length() && b->content_session_id.length())
                    {
                        size_t off = res.find_first_of("://");
                        if (off != std::string::npos)
                        {
                            char tmp[1024];
                            sprintf(tmp, "%s.%s/",
                                    b->content_session_id.c_str(),
                                    m_p->content_proxy_server.c_str());
                            res.insert(off + 3, tmp);
                        }
                    }
                    if (res.length())
                    {
                        xmlNode *ptr = xmlDocGetRootElement(doc);
                        while (ptr && ptr->type != XML_ELEMENT_NODE)
                            ptr = ptr->next;
                        xmlNode *c = 
                            xmlNewChild(ptr, 0, BAD_CAST "generated-url", 0);
                        xmlNode * t = xmlNewText(BAD_CAST res.c_str());
                        xmlAddChild(c, t);

                        if (xmlrec_buf)
                            xmlFree(xmlrec_buf);

                        xmlDocDumpMemory(doc, &xmlrec_buf, &rec_len);
                        rec_buf = (const char *) xmlrec_buf;
                    }
                    xmlFreeDoc(doc);
                }
                if (!npr)
                {
                    if (!rec_buf)
                        npr = zget_surrogateDiagRec(
                            odr, odr_database, 
                            YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS,
                            rec_type_str);
                    else
                    {
                        npr = (Z_NamePlusRecord *)
                            odr_malloc(odr, sizeof(*npr));
                        npr->databaseName = odr_database;
                        npr->which = Z_NamePlusRecord_databaseRecord;
                        npr->u.databaseRecord =
                            z_ext_record_xml(odr, rec_buf, rec_len);
                    }
                }
                if (xmlrec_buf)
                    xmlFree(xmlrec_buf);
            }
            else
            {
                Z_External *ext =
                    (Z_External *) ZOOM_record_get(recs[i], "ext", 0);
                if (ext)
                {
                    npr = (Z_NamePlusRecord *) odr_malloc(odr, sizeof(*npr));
                    npr->databaseName = odr_database;
                    npr->which = Z_NamePlusRecord_databaseRecord;
                    npr->u.databaseRecord = ext;
                }
                else
                {
                    npr = zget_surrogateDiagRec(
                        odr, odr_database, 
                        YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS,
                        "ZOOM_record, type ext");
                }
            }
            npl->records[i] = npr;
        }
        records = (Z_Records*) odr_malloc(odr, sizeof(*records));
        records->which = Z_Records_DBOSD;
        records->u.databaseOrSurDiagnostics = npl;
    }
    return records;
}

struct cql_node *yf::Zoom::Impl::convert_cql_fields(struct cql_node *cn,
                                                    ODR odr)
{
    struct cql_node *r = 0;
    if (!cn)
        return 0;
    switch (cn->which)
    {
    case CQL_NODE_ST:
        if (cn->u.st.index)
        {
            std::map<std::string,std::string>::const_iterator it;
            it = fieldmap.find(cn->u.st.index);
            if (it == fieldmap.end())
                return cn;
            if (it->second.length())
                cn->u.st.index = odr_strdup(odr, it->second.c_str());
            else
                cn->u.st.index = 0;
        }
        break;
    case CQL_NODE_BOOL:
        r = convert_cql_fields(cn->u.boolean.left, odr);
        if (!r)
            r = convert_cql_fields(cn->u.boolean.right, odr);
        break;
    case CQL_NODE_SORT:
        r = convert_cql_fields(cn->u.sort.search, odr);
        break;
    }
    return r;
}

static void sort_pqf_type_7(WRBUF pqf_wrbuf, const char *sru_sortkeys)
{
    /* sortkey layour: path,schema,ascending,caseSensitive,missingValue */
    /* see cql_sortby_to_sortkeys of YAZ. */
    char **sortspec;
    int num_sortspec = 0;
    int i;
    NMEM nmem = nmem_create();
    
    if (sru_sortkeys)
        nmem_strsplit_blank(nmem, sru_sortkeys, &sortspec, &num_sortspec);
    if (num_sortspec > 0)
    {
        WRBUF w = wrbuf_alloc();
        for (i = 0; i < num_sortspec; i++)
        {
            char **arg;
            int num_arg;
            int ascending = 1;
            nmem_strsplitx(nmem, ",", sortspec[i], &arg, &num_arg, 0);
            
            if (num_arg > 2 && arg[2][0])
                ascending = atoi(arg[2]);
            
            wrbuf_puts(w, "@or @attr 1=");
            yaz_encode_pqf_term(w, arg[0], strlen(arg[0]));
            wrbuf_printf(w, "@attr 7=%d %d ", ascending ? 1 : 2, i);
        }
        if (wrbuf_len(w))
        {
            wrbuf_puts(w, wrbuf_cstr(pqf_wrbuf));
            wrbuf_rewind(pqf_wrbuf);
            wrbuf_puts(pqf_wrbuf, wrbuf_cstr(w));
        }
        wrbuf_destroy(w);
    }
    nmem_destroy(nmem);
}

static void sort_via_cql(WRBUF cql_sortby, const char *sru_sortkeys)
{
    /* sortkey layour: path,schema,ascending,caseSensitive,missingValue */
    /* see cql_sortby_to_sortkeys of YAZ. */
    char **sortspec;
    int num_sortspec = 0;
    int i;
    NMEM nmem = nmem_create();
    
    if (sru_sortkeys)
        nmem_strsplit_blank(nmem, sru_sortkeys, &sortspec, &num_sortspec);
    if (num_sortspec > 0)
    {
        WRBUF w = wrbuf_alloc();
        for (i = 0; i < num_sortspec; i++)
        {
            char **arg;
            int num_arg;
            int ascending = 1;
            int case_sensitive = 0;
            const char *missing = 0;
            nmem_strsplitx(nmem, ",", sortspec[i], &arg, &num_arg, 0);
            
            if (num_arg > 2 && arg[2][0])
                ascending = atoi(arg[2]);
            if (num_arg > 3 && arg[3][0])
                case_sensitive = atoi(arg[3]);
            if (num_arg > 4 && arg[4][0])
                missing = arg[4];
            if (i > 0)
                wrbuf_puts(w, " ");
            else
                wrbuf_puts(w, " sortby ");
            wrbuf_puts(w, arg[0]);  /* field */
            wrbuf_puts(w, "/");
            wrbuf_puts(w, ascending ? "ascending" : "descending");
            if (case_sensitive)
                wrbuf_puts(w, "/respectCase");
            if (missing)
            {
                if (!strcmp(missing, "omit"))
                    wrbuf_puts(w, "/missingOmit");
                else if (!strcmp(missing, "abort"))
                    wrbuf_puts(w, "/missingFail");
                else if (!strcmp(missing, "lowValue"))
                    wrbuf_puts(w, "/missingLow");
                else if (!strcmp(missing, "highValue"))
                    wrbuf_puts(w, "/missingHigh");
            }
        }
        if (wrbuf_len(w))
            wrbuf_puts(cql_sortby, wrbuf_cstr(w));
        wrbuf_destroy(w);
    }
    nmem_destroy(nmem);
}

#if YAZ_VERSIONL < 0x40206
static void wrbuf_vp_puts(const char *buf, void *client_data)
{
    WRBUF b = (WRBUF) client_data;
    wrbuf_puts(b, buf);
}
#endif

void yf::Zoom::Frontend::handle_search(mp::Package &package)
{
    Z_GDU *gdu = package.request().get();
    Z_APDU *apdu_req = gdu->u.z3950;
    Z_APDU *apdu_res = 0;
    mp::odr odr;
    Z_SearchRequest *sr = apdu_req->u.searchRequest;
    if (sr->num_databaseNames != 1)
    {
        apdu_res = odr.create_searchResponse(
            apdu_req, YAZ_BIB1_TOO_MANY_DATABASES_SPECIFIED, 0);
        package.response() = apdu_res;
        return;
    }

    int error = 0;
    char *addinfo = 0;
    std::string db(sr->databaseNames[0]);
    BackendPtr b = get_backend_from_databases(db, &error, &addinfo, odr);
    if (error)
    {
        apdu_res = 
            odr.create_searchResponse(apdu_req, error, addinfo);
        package.response() = apdu_res;
        return;
    }

    b->set_option("setname", "default");

    bool enable_pz2_retrieval = false;
    bool enable_pz2_transform = false;
    bool assume_marc8_charset = false;
    prepare_elements(b, sr->preferredRecordSyntax, 0 /*element_set_name */,
                     enable_pz2_retrieval,
                     enable_pz2_transform,
                     assume_marc8_charset);

    Odr_int hits = 0;
    Z_Query *query = sr->query;
    WRBUF ccl_wrbuf = 0;
    WRBUF pqf_wrbuf = 0;
    std::string sru_sortkeys;

    if (query->which == Z_Query_type_1 || query->which == Z_Query_type_101)
    {
        // RPN
        pqf_wrbuf = wrbuf_alloc();
        yaz_rpnquery_to_wrbuf(pqf_wrbuf, query->u.type_1);
    }
    else if (query->which == Z_Query_type_2)
    {
        // CCL
        ccl_wrbuf = wrbuf_alloc();
        wrbuf_write(ccl_wrbuf, (const char *) query->u.type_2->buf,
                    query->u.type_2->len);
    }
    else if (query->which == Z_Query_type_104 &&
             query->u.type_104->which == Z_External_CQL)
    {
        // CQL
        const char *cql = query->u.type_104->u.cql;
        CQL_parser cp = cql_parser_create();
        int r = cql_parser_string(cp, cql);
        if (r)
        {
            cql_parser_destroy(cp);
            apdu_res = 
                odr.create_searchResponse(apdu_req, 
                                          YAZ_BIB1_MALFORMED_QUERY,
                                          "CQL syntax error");
            package.response() = apdu_res;
            return;
        }
        struct cql_node *cn = cql_parser_result(cp);
        struct cql_node *cn_error = m_p->convert_cql_fields(cn, odr);
        if (cn_error)
        {
            // hopefully we are getting a ptr to a index+relation+term node
            addinfo = 0;
            if (cn_error->which == CQL_NODE_ST)
                addinfo = cn_error->u.st.index;

            apdu_res = 
                odr.create_searchResponse(apdu_req, 
                                          YAZ_BIB1_UNSUPP_USE_ATTRIBUTE,
                                          addinfo);
            package.response() = apdu_res;
            return;
        }
        char ccl_buf[1024];

        r = cql_to_ccl_buf(cn, ccl_buf, sizeof(ccl_buf));
        if (r == 0)
        {
            ccl_wrbuf = wrbuf_alloc();
            wrbuf_puts(ccl_wrbuf, ccl_buf);
            
            WRBUF sru_sortkeys_wrbuf = wrbuf_alloc();

            cql_sortby_to_sortkeys(cn, wrbuf_vp_puts, sru_sortkeys_wrbuf);

            sru_sortkeys.assign(wrbuf_cstr(sru_sortkeys_wrbuf));
            wrbuf_destroy(sru_sortkeys_wrbuf);
        }
        cql_parser_destroy(cp);
        if (r)
        {
            apdu_res = 
                odr.create_searchResponse(apdu_req, 
                                          YAZ_BIB1_MALFORMED_QUERY,
                                          "CQL to CCL conversion error");
            package.response() = apdu_res;
            return;
        }
    }
    else
    {
        apdu_res = 
            odr.create_searchResponse(apdu_req, YAZ_BIB1_QUERY_TYPE_UNSUPP, 0);
        package.response() = apdu_res;
        return;
    }

    if (ccl_wrbuf)
    {
        // CCL to PQF
        assert(pqf_wrbuf == 0);
        int cerror, cpos;
        struct ccl_rpn_node *cn;
        yaz_log(YLOG_LOG, "CCL: %s", wrbuf_cstr(ccl_wrbuf));
        cn = ccl_find_str(b->sptr->ccl_bibset, wrbuf_cstr(ccl_wrbuf),
                          &cerror, &cpos);
        wrbuf_destroy(ccl_wrbuf);
        if (!cn)
        {
            char *addinfo = odr_strdup(odr, ccl_err_msg(cerror));
            int z3950_diag = YAZ_BIB1_MALFORMED_QUERY;

            switch (cerror)
            {
            case CCL_ERR_UNKNOWN_QUAL:
                z3950_diag = YAZ_BIB1_UNSUPP_USE_ATTRIBUTE;
                break;
            case CCL_ERR_TRUNC_NOT_LEFT: 
            case CCL_ERR_TRUNC_NOT_RIGHT:
            case CCL_ERR_TRUNC_NOT_BOTH:
                z3950_diag = YAZ_BIB1_UNSUPP_TRUNCATION_ATTRIBUTE;
                break;
            }
            apdu_res = 
                odr.create_searchResponse(apdu_req, z3950_diag, addinfo);
            package.response() = apdu_res;
            return;
        }
        pqf_wrbuf = wrbuf_alloc();
        ccl_pquery(pqf_wrbuf, cn);
        ccl_rpn_delete(cn);
    }
    
    assert(pqf_wrbuf);
    if (b->get_option("sru"))
    {
        int status = 0;
        Z_RPNQuery *zquery;
        zquery = p_query_rpn(odr, wrbuf_cstr(pqf_wrbuf));
        WRBUF wrb = wrbuf_alloc();
            
        if (!strcmp(b->get_option("sru"), "solr"))
        {
            solr_transform_t cqlt = solr_transform_create();
            
            status = solr_transform_rpn2solr_wrbuf(cqlt, wrb, zquery);
            
            solr_transform_close(cqlt);
        }
        else
        {
            cql_transform_t cqlt = cql_transform_create();
            
            status = cql_transform_rpn2cql_wrbuf(cqlt, wrb, zquery);
            
            cql_transform_close(cqlt);

            if (status == 0)
                sort_via_cql(wrb, sru_sortkeys.c_str());
        }
        if (status == 0)
        {
            yaz_log(YLOG_LOG, "search CQL: %s", wrbuf_cstr(wrb));
            b->search_cql(wrbuf_cstr(wrb), &hits, &error, &addinfo, odr);
        }
        
        wrbuf_destroy(wrb);
        wrbuf_destroy(pqf_wrbuf);
        if (status)
        {
            apdu_res = 
                odr.create_searchResponse(apdu_req, YAZ_BIB1_MALFORMED_QUERY,
                                          "can not convert from RPN to CQL/SOLR");
            package.response() = apdu_res;
            return;
        }
    }
    else
    {
        sort_pqf_type_7(pqf_wrbuf, sru_sortkeys.c_str());

        yaz_log(YLOG_LOG, "search PQF: %s", wrbuf_cstr(pqf_wrbuf));
        b->search_pqf(wrbuf_cstr(pqf_wrbuf), &hits, &error, &addinfo, odr);
        wrbuf_destroy(pqf_wrbuf);
    }

    const char *element_set_name = 0;
    Odr_int number_to_present = 0;
    if (!error)
        mp::util::piggyback_sr(sr, hits, number_to_present, &element_set_name);
    
    Odr_int number_of_records_returned = 0;
    Z_Records *records = get_records(
        0, number_to_present, &error, &addinfo,
        &number_of_records_returned, odr, b, sr->preferredRecordSyntax,
        element_set_name);
    apdu_res = odr.create_searchResponse(apdu_req, error, addinfo);
    if (records)
    {
        apdu_res->u.searchResponse->records = records;
        apdu_res->u.searchResponse->numberOfRecordsReturned =
            odr_intdup(odr, number_of_records_returned);
    }
    apdu_res->u.searchResponse->resultCount = odr_intdup(odr, hits);
    package.response() = apdu_res;
}

void yf::Zoom::Frontend::handle_present(mp::Package &package)
{
    Z_GDU *gdu = package.request().get();
    Z_APDU *apdu_req = gdu->u.z3950;
    Z_APDU *apdu_res = 0;
    Z_PresentRequest *pr = apdu_req->u.presentRequest;

    mp::odr odr;
    if (!m_backend)
    {
        package.response() = odr.create_presentResponse(
            apdu_req, YAZ_BIB1_SPECIFIED_RESULT_SET_DOES_NOT_EXIST, 0);
        return;
    }
    const char *element_set_name = 0;
    Z_RecordComposition *comp = pr->recordComposition;
    if (comp && comp->which != Z_RecordComp_simple)
    {
        package.response() = odr.create_presentResponse(
            apdu_req, 
            YAZ_BIB1_PRESENT_COMP_SPEC_PARAMETER_UNSUPP, 0);
        return;
    }
    if (comp && comp->u.simple->which == Z_ElementSetNames_generic)
        element_set_name = comp->u.simple->u.generic;
    Odr_int number_of_records_returned = 0;
    int error = 0;
    char *addinfo = 0;
    Z_Records *records = get_records(
        *pr->resultSetStartPoint - 1, *pr->numberOfRecordsRequested,
        &error, &addinfo, &number_of_records_returned, odr, m_backend,
        pr->preferredRecordSyntax, element_set_name);

    apdu_res = odr.create_presentResponse(apdu_req, error, addinfo);
    if (records)
    {
        apdu_res->u.presentResponse->records = records;
        apdu_res->u.presentResponse->numberOfRecordsReturned =
            odr_intdup(odr, number_of_records_returned);
    }
    package.response() = apdu_res;
}

void yf::Zoom::Frontend::handle_package(mp::Package &package)
{
    Z_GDU *gdu = package.request().get();
    if (!gdu)
        ;
    else if (gdu->which == Z_GDU_Z3950)
    {
        Z_APDU *apdu_req = gdu->u.z3950;
        if (apdu_req->which == Z_APDU_initRequest)
        {
            mp::odr odr;
            package.response() = odr.create_close(
                apdu_req,
                Z_Close_protocolError,
                "double init");
        }
        else if (apdu_req->which == Z_APDU_searchRequest)
        {
            handle_search(package);
        }
        else if (apdu_req->which == Z_APDU_presentRequest)
        {
            handle_present(package);
        }
        else
        {
            mp::odr odr;
            package.response() = odr.create_close(
                apdu_req,
                Z_Close_protocolError,
                "zoom filter cannot handle this APDU");
            package.session().close();
        }
    }
    else
    {
        package.session().close();
    }
}

void yf::Zoom::Impl::process(mp::Package &package)
{
    FrontendPtr f = get_frontend(package);
    Z_GDU *gdu = package.request().get();

    if (f->m_is_virtual)
    {
        f->handle_package(package);
    }
    else if (gdu && gdu->which == Z_GDU_Z3950 && gdu->u.z3950->which ==
             Z_APDU_initRequest)
    {
        Z_InitRequest *req = gdu->u.z3950->u.initRequest;
        f->m_init_gdu = gdu;
        
        mp::odr odr;
        Z_APDU *apdu = odr.create_initResponse(gdu->u.z3950, 0, 0);
        Z_InitResponse *resp = apdu->u.initResponse;
        
        int i;
        static const int masks[] = {
            Z_Options_search,
            Z_Options_present,
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
        
        *resp->preferredMessageSize = *req->preferredMessageSize;
        *resp->maximumRecordSize = *req->maximumRecordSize;
        
        package.response() = apdu;
        f->m_is_virtual = true;
    }
    else
        package.move();

    release_frontend(package);
}


static mp::filter::Base* filter_creator()
{
    return new mp::filter::Zoom;
}

extern "C" {
    struct metaproxy_1_filter_struct metaproxy_1_filter_zoom = {
        0,
        "zoom",
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

