/* This file is part of Metaproxy.
   Copyright (C) 2005-2013 Index Data

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
#include <iostream>
#include <stdexcept>

#include "filter_http_client.hpp"
#include <metaproxy/util.hpp>
#include "router_chain.hpp"
#include <metaproxy/package.hpp>

#define BOOST_REGEX_MATCH_EXTRA

#include <boost/xpressive/xpressive.hpp>
#include <boost/lexical_cast.hpp>

#define BOOST_AUTO_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/auto_unit_test.hpp>

using namespace boost::unit_test;
using namespace boost::xpressive;
namespace mp = metaproxy_1;

class FilterHeaderRewrite: public mp::filter::Base {
public:
    void process(mp::Package & package) const {
        Z_GDU *gdu = package.request().get();
        //we have an http req
        if (gdu && gdu->which == Z_GDU_HTTP_Request)
        {
          std::cout << "Request headers" << std::endl;
          Z_HTTP_Request *hreq = gdu->u.HTTP_Request;
          //dump req headers
          for (Z_HTTP_Header *header = hreq->headers;
              header != 0; 
              header = header->next) 
          {
            std::cout << header->name << ": " << header->value << std::endl;
            rewrite_req_header(header);
          }
        }
        package.move();
        gdu = package.response().get();
        if (gdu && gdu->which == Z_GDU_HTTP_Response)
        {
          std::cout << "Respose headers" << std::endl;
          Z_HTTP_Response *hr = gdu->u.HTTP_Response;
          //dump resp headers
          for (Z_HTTP_Header *header = hr->headers;
              header != 0; 
              header = header->next) 
          {
            std::cout << header->name << ": " << header->value << std::endl;
          }
        }

    };
    void configure(const xmlNode* ptr, bool test_only, const char *path) {};

    void rewrite_req_header(Z_HTTP_Header *header) const
    {
        //exec regex against value
        sregex re = sregex::compile(req_uri_rx);
        smatch what;
        std::string hvalue(header->value);
        std::map<std::string, std::string> vars;
        if (regex_match(hvalue, what, re))
        {
            unsigned i;
            for (i = 1; i < what.size(); ++i)
            {
                //check if the group is named
                std::map<int, std::string>::const_iterator it
                    = groups_by_num.find(i);
                if (it != groups_by_num.end()) 
                {   //it is
                    std::string name = it->second;
                    vars[name] = what[i];
                }
            }
            //rewrite the header according to the recipe
            std::string rvalue = sub_vars(req_uri_pat, vars);
            std::cout << "Rewritten '"+hvalue+"' to '"+rvalue+"'\n";
        }
        else
        {
            std::cout << "No match found in '" + hvalue + "'\n";
        }
    };

    static void parse_groups(const std::string & str,
            std::map<int, std::string> & groups_bynum,
            std::map<std::string, int> & groups_byname)
    {
       int gnum = 0;
       bool esc = false;
       for (int i = 0; i < str.size(); ++i)
       {
           if (!esc && str[i] == '\\')
           {
               esc = true;
               continue;
           }
           if (!esc && str[i] == '(') //group starts
           {
               gnum++;
               if (i+1 < str.size() && str[i+1] == '?') //group with attrs 
               {
                   i++;
                   if (i+1 < str.size() && str[i+1] == 'P') //optional, python
                       i++;
                   if (i+1 < str.size() && str[i+1] == '<') //named
                   {
                       i++;
                       std::string gname;
                       bool term = false;
                       while (++i < str.size())
                       {
                           if (str[i] == '>') { term = true; break; }
                           if (!isalnum(str[i])) 
                               throw mp::filter::FilterException
                                   ("Only alphanumeric chars allowed, found "
                                    " in '" 
                                    + str 
                                    + "' at " 
                                    + boost::lexical_cast<std::string>(i)); 
                           gname += str[i];
                       }
                       if (!term)
                           throw mp::filter::FilterException
                               ("Unterminated group name '" + gname 
                                + " in '" + str +"'");
                      groups_bynum[gnum] = gname;
                      groups_byname[gname] = gnum;
                      std::cout << "Found named group '" << gname 
                          << "' at $" << gnum << std::endl;
                   }
               }
           }
           esc = false;
       }
    }

    static std::string sub_vars (const std::string & in, 
            const std::map<std::string, std::string> & vars)
    {
        std::string out;
        bool esc = false;
        for (int i = 0; i < in.size(); ++i)
        {
            if (!esc && in[i] == '\\')
            {
                esc = true;
                continue;
            }
            if (!esc && in[i] == '$') //var
            {
                if (i+1 < in.size() && in[i+1] == '{') //ref prefix
                {
                    ++i;
                    std::string name;
                    bool term = false;
                    while (++i < in.size()) 
                    {
                        if (in[i] == '}') { term = true; break; }
                        name += in[i];
                    }
                    if (!term) throw mp::filter::FilterException
                        ("Unterminated var ref in '"+in+"' at "
                         + boost::lexical_cast<std::string>(i));
                    std::map<std::string, std::string>::const_iterator it
                        = vars.find(name);
                    if (it != vars.end())
                        out += it->second;
                }
                else
                {
                    throw mp::filter::FilterException
                        ("Malformed or trimmed var ref in '"
                         +in+"' at "+boost::lexical_cast<std::string>(i)); 
                }
                continue;
            }
            //passthru
            out += in[i];
            esc = false;
        }
        return out;
    }
    
    void configure(
            const std::string & req_uri_rx, 
            const std::string & req_uri_pat, 
            const std::string & resp_uri_rx, 
            const std::string & resp_uri_pat) 
    {
       this->req_uri_rx = req_uri_rx;
       this->req_uri_pat = req_uri_pat;
       //pick up names
       parse_groups(req_uri_rx, groups_by_num, groups_by_name);
       this->resp_uri_rx = resp_uri_rx;
       this->resp_uri_pat = resp_uri_pat;
    };

private:
    std::map<std::string, std::string> vars;
    std::string req_uri_rx;
    std::string resp_uri_rx;
    std::string req_uri_pat;
    std::string resp_uri_pat;
    std::map<int, std::string> groups_by_num;
    std::map<std::string, int> groups_by_name;

};


BOOST_AUTO_TEST_CASE( test_filter_rewrite_1 )
{
    try
    {
       FilterHeaderRewrite fhr;
    }
    catch ( ... ) {
        BOOST_CHECK (false);
    }
}

BOOST_AUTO_TEST_CASE( test_filter_rewrite_2 )
{
    try
    {
        mp::RouterChain router;

        FilterHeaderRewrite fhr;
        fhr.configure(
                ".*?(?P<host>[^:]+):(?P<port>\\d+).*",
                "http://${host}:${port}/somepath",
                ".*(localhost).*",
                "http:://g");
        mp::filter::HTTPClient hc;
        
        router.append(fhr);
        router.append(hc);

        // create an http request
        mp::Package pack;

        mp::odr odr;
        Z_GDU *gdu_req = z_get_HTTP_Request_uri(odr, 
            "http://localhost:80/~jakub/targetsite.php", 0, 1);

        pack.request() = gdu_req;

        //feed to the router
        pack.router(router).move();

        //analyze the response
        Z_GDU *gdu_res = pack.response().get();
        BOOST_CHECK(gdu_res);
        BOOST_CHECK_EQUAL(gdu_res->which, Z_GDU_HTTP_Response);
        
        Z_HTTP_Response *hres = gdu_res->u.HTTP_Response;
        BOOST_CHECK(hres);

    }
    catch (std::exception & e) {
        std::cout << e.what();
        BOOST_CHECK (false);
    }
}

/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

