/* $Id: ex_router_flexml.cpp,v 1.11 2007-05-09 21:23:09 adam Exp $
   Copyright (c) 2005-2007, Index Data.

This file is part of Metaproxy.

Metaproxy is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

Metaproxy is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with Metaproxy; see the file LICENSE.  If not, write to the
Free Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.
 */

#include "config.hpp"

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <iostream>
#include <stdexcept>

#include "filter.hpp"
#include "package.hpp"
#include "router_flexml.hpp"
#include "factory_static.hpp"

namespace mp = metaproxy_1;

int main(int argc, char **argv)
{
    try 
    {
        po::options_description desc("Allowed options");
        desc.add_options()
            ("help", "produce help message")
            ("config", po::value< std::vector<std::string> >(), "xml config")
            ;
        
        po::positional_options_description p;
        p.add("config", -1);
        
        po::variables_map vm;        
        po::store(po::command_line_parser(argc, argv).
                  options(desc).positional(p).run(), vm);
        po::notify(vm);    
        
        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 1;
        }
        
        xmlDocPtr doc = 0;
        if (vm.count("config"))
        {
            std::vector<std::string> config_fnames = 
                vm["config"].as< std::vector<std::string> >();

            if (config_fnames.size() != 1)
            {
                std::cerr << "Only one configuration must be given\n";
                std::exit(1);
            }
            
            doc = xmlParseFile(config_fnames[0].c_str());
            if (!doc)
            {
                std::cerr << "xmlParseFile failed\n";
                std::exit(1);
            }
        }
        else
        {
            std::cerr << "No configuration given\n";
            std::exit(1);
        }
        if (doc)
        {
            mp::FactoryStatic factory;
            mp::RouterFleXML router(doc, factory);

	    mp::Package pack;
	 
            pack.router(router).move();

            xmlFreeDoc(doc);
        }
    }
    catch ( ... ) {
        std::cerr << "Unknown Exception" << std::endl;
        throw;
        std::exit(1);
    }
    std::exit(0);
}


/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
