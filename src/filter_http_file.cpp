/* $Id: filter_http_file.cpp,v 1.5 2006-06-10 14:29:12 adam Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#include "config.hpp"

#include "filter.hpp"
#include "package.hpp"

#include <boost/thread/mutex.hpp>

#include "util.hpp"
#include "filter_http_file.hpp"

#include <list>
#include <map>

#include <yaz/zgdu.h>

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

namespace mp = metaproxy_1;
namespace yf = mp::filter;

namespace metaproxy_1 {
    namespace filter {
        struct HttpFile::Area {
            std::string m_url_path_prefix;
            std::string m_file_root;
        };
        class HttpFile::Mime {
            friend class Rep;
            std::string m_type;
        public:
            Mime(std::string type);
            Mime();
        };
        class HttpFile::Rep {
            friend class HttpFile;

            typedef std::list<Area> AreaList;
            typedef std::map<std::string,Mime> MimeMap;

            MimeMap m_ext_to_map;
            AreaList m_area_list;
            void fetch_uri(mp::Session &session,
                           Z_HTTP_Request *req, mp::Package &package);
            void fetch_file(mp::Session &session,
                            Z_HTTP_Request *req,
                            std::string &fname, mp::Package &package);
            std::string get_mime_type(std::string &fname);
        };
    }
}

yf::HttpFile::Mime::Mime() {}

yf::HttpFile::Mime::Mime(std::string type) : m_type(type) {}

yf::HttpFile::HttpFile() : m_p(new Rep)
{
#if 0
    m_p->m_ext_to_map["html"] = Mime("text/html");
    m_p->m_ext_to_map["htm"] = Mime("text/html");
    m_p->m_ext_to_map["png"] = Mime("image/png");
    m_p->m_ext_to_map["txt"] = Mime("text/plain");
    m_p->m_ext_to_map["text"] = Mime("text/plain");
    m_p->m_ext_to_map["asc"] = Mime("text/plain");
    m_p->m_ext_to_map["xml"] = Mime("application/xml");
    m_p->m_ext_to_map["xsl"] = Mime("application/xml");
#endif
#if 0
    Area a;
    a.m_url_path_prefix = "/etc";
    a.m_file_root = ".";
    m_p->m_area_list.push_back(a);
#endif
}

yf::HttpFile::~HttpFile()
{
}

std::string yf::HttpFile::Rep::get_mime_type(std::string &fname)
{
    std::string file_part = fname;
    std::string::size_type p = fname.find_last_of('/');
    
    if (p != std::string::npos)
        file_part = fname.substr(p+1);

    p = file_part.find_last_of('.');
    std::string content_type;
    if (p != std::string::npos)
    {
        std::string ext = file_part.substr(p+1);
        MimeMap::const_iterator it = m_ext_to_map.find(ext);

        if (it != m_ext_to_map.end())
            content_type = it->second.m_type;
    }
    if (content_type.length() == 0)
        content_type = "application/octet-stream";
    return content_type;
}

void yf::HttpFile::Rep::fetch_file(mp::Session &session,
                                   Z_HTTP_Request *req,
                                   std::string &fname, mp::Package &package)
{
    mp::odr o;
    
    FILE *f = fopen(fname.c_str(), "rb");
    if (!f)
    {
        Z_GDU *gdu = o.create_HTTP_Response(session, req, 404);
        package.response() = gdu;
        return;
    }
    if (fseek(f, 0L, SEEK_END) == -1)
    {
        fclose(f);
        Z_GDU *gdu = o.create_HTTP_Response(session, req, 404);
        package.response() = gdu;
        return;
    }
    long sz = ftell(f);
    if (sz > 1000000L)
    {
        fclose(f);
        Z_GDU *gdu = o.create_HTTP_Response(session, req, 404);
        package.response() = gdu;
        return;
    }
    rewind(f);

    Z_GDU *gdu = o.create_HTTP_Response(session, req, 200);

    Z_HTTP_Response *hres = gdu->u.HTTP_Response;
    hres->content_len = sz;
    hres->content_buf = (char*) odr_malloc(o, hres->content_len);
    fread(hres->content_buf, 1, hres->content_len, f);

    fclose(f);
    
    std::string content_type = get_mime_type(fname);

    z_HTTP_header_add(o, &hres->headers,
                      "Content-Type", content_type.c_str());
    package.response() = gdu;
}

void yf::HttpFile::Rep::fetch_uri(mp::Session &session,
                                  Z_HTTP_Request *req, mp::Package &package)
{
    bool sane = true;
    std::string path = req->path;

    // we don't consider ?, # yet..

    // we don't allow ..
    std::string::size_type p = path.find("..");
    if (p != std::string::npos)
        sane = false;

    if (sane)
    {
        AreaList::const_iterator it;
        for (it = m_area_list.begin(); it != m_area_list.end(); it++)
        {
            std::string::size_type l = it->m_url_path_prefix.length();

            if (path.compare(0, l, it->m_url_path_prefix) == 0)
            {
                std::string fname = it->m_file_root + path.substr(l);
                std::cout << "fname = " << fname << "\n";
                fetch_file(session, req, fname, package);
                return;
            }
        }
    }
    mp::odr o;
    Z_GDU *gdu = o.create_HTTP_Response(session, req, 404);
    package.response() = gdu;
}
                         
void yf::HttpFile::process(mp::Package &package) const
{
    Z_GDU *gdu = package.request().get();
    if (gdu && gdu->which == Z_GDU_HTTP_Request)
        m_p->fetch_uri(package.session(), gdu->u.HTTP_Request, package);
    else
        package.move();
}

void mp::filter::HttpFile::configure(const xmlNode * ptr)
{
    for (ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        if (!strcmp((const char *) ptr->name, "mimetypes"))
        {
            std::string fname = mp::xml::get_text(ptr);

            mp::PlainFile f;

            if (!f.open(fname))
            {
                throw mp::filter::FilterException
                    ("Can not open mime types file " + fname);
            }
            
            std::vector<std::string> args;
            while (f.getline(args))
            {
                size_t i;
                for (i = 1; i<args.size(); i++)
                    m_p->m_ext_to_map[args[i]] = args[0];
            }
        }
        else if (!strcmp((const char *) ptr->name, "area"))
        {
            xmlNode *a_node = ptr->children;
            Area a;
            for (; a_node; a_node = a_node->next)
            {
                if (a_node->type != XML_ELEMENT_NODE)
                    continue;
                
                if (mp::xml::is_element_yp2(a_node, "documentroot"))
                    a.m_file_root = mp::xml::get_text(a_node);
                else if (mp::xml::is_element_yp2(a_node, "prefix"))
                    a.m_url_path_prefix = mp::xml::get_text(a_node);
                else
                    throw mp::filter::FilterException
                        ("Bad element " 
                         + std::string((const char *) a_node->name)
                         + " in area section"
                            );
            }
            if (a.m_file_root.length())
            {
                m_p->m_area_list.push_back(a);
            }
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
    return new mp::filter::HttpFile;
}

extern "C" {
    struct metaproxy_1_filter_struct metaproxy_1_filter_http_file = {
        0,
        "http_file",
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
