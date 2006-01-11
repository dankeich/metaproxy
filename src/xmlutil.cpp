/* $Id: xmlutil.cpp,v 1.2 2006-01-11 11:51:50 adam Exp $
   Copyright (c) 2005, Index Data.

%LICENSE%
 */

#include "xmlutil.hpp"

std::string yp2::xml::get_text(const xmlNode *ptr)
{
    std::string c;
    for (ptr = ptr->children; ptr; ptr = ptr->next)
        if (ptr->type == XML_TEXT_NODE)
            c += std::string((const char *) (ptr->content));
    return c;
}


bool yp2::xml::is_element(const xmlNode *ptr, 
                          const std::string &ns,
                          const std::string &name)
{
    if (ptr && ptr->type == XML_ELEMENT_NODE && ptr->ns && ptr->ns->href 
        && !xmlStrcmp(BAD_CAST ns.c_str(), ptr->ns->href)
        && !xmlStrcmp(BAD_CAST name.c_str(), ptr->name))
        return true;
    return false;
}

bool yp2::xml::is_element_yp2(const xmlNode *ptr, 
                              const std::string &name)
{
    return yp2::xml::is_element(ptr, "http://indexdata.dk/yp2/config/1", name);
}

std::string yp2::xml::get_route(const xmlNode *node)
{
    std::string route_value;
    if (node)
    {
        const struct _xmlAttr *attr;
        for (attr = node->properties; attr; attr = attr->next)
        {
            std::string name = std::string((const char *) attr->name);
            std::string value;
            
            if (attr->children && attr->children->type == XML_TEXT_NODE)
                value = std::string((const char *)attr->children->content);
            
            if (name == "route")
                route_value = value;
            else
                throw XMLError("Only attribute route allowed"
                               " in " + std::string((const char *)node->name)
                               + " element. Got " + std::string(name));
        }
    }
    return route_value;
}

/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
