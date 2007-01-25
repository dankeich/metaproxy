/* $Id: router_flexml.hpp,v 1.16 2007-01-25 14:05:54 adam Exp $
   Copyright (c) 2005-2007, Index Data.

      See the LICENSE file for details
*/

#ifndef ROUTER_FLEXML_HPP
#define ROUTER_FLEXML_HPP

#include "router.hpp"

#include "factory_filter.hpp"

#include <stdexcept>

#include <boost/scoped_ptr.hpp>

namespace metaproxy_1 
{
    class RouterFleXML : public metaproxy_1::Router 
    {
        class Rep;
        class Route;
        class Pos;
    public:
        RouterFleXML(std::string xmlconf, metaproxy_1::FactoryFilter &factory);
        RouterFleXML(xmlDocPtr doc, metaproxy_1::FactoryFilter &factory);
        
        ~RouterFleXML();

        virtual RoutePos *createpos() const;
        class XMLError1 : public std::runtime_error {
        public:
            XMLError1(const std::string msg) :
                std::runtime_error("XMLError : " + msg) {} ;
        };
    private:
        boost::scoped_ptr<Rep> m_p;
    };
 
};
#endif

/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
