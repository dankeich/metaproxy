/* $Id: metaproxy_prog.cpp,v 1.7 2006-11-29 22:37:08 marc Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#include "config.hpp"

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <iostream>
#include <stdexcept>
#include <libxml/xinclude.h>

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
            ("help,h", "produce help message")
            ("version,V", "show version")
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
        if (vm.count("version")) {
            std::cout << "Metaproxy " VERSION "\n";
            return 0;
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
            
            // need to parse with xinclude tags 
            // XML_PARSE_XINCLUDE XML_PARSE_NOBLANKS  
            // XML_PARSE_NSCLEAN XML_PARSE_NONET 
            doc = xmlReadFile(config_fnames[0].c_str(), 
                              NULL, 
                              XML_PARSE_XINCLUDE + XML_PARSE_NOBLANKS
                              + XML_PARSE_NSCLEAN + XML_PARSE_NONET );

            if (!doc)
            {
                std::cerr << "XML parsing failed\n";
                std::exit(1);
            }
            // and perform Xinclude then
            if (xmlXIncludeProcess(doc) <= 0) {
                std::cerr << "XInclude processing failed\n";
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
            try {
                mp::FactoryStatic factory;
                mp::RouterFleXML router(doc, factory);
                mp::Package pack;
                pack.router(router).move();
            }
            catch (std::runtime_error &e) {
                std::cerr << "std::runtime error: " << e.what() << "\n";
                std::exit(1);
            }
            xmlFreeDoc(doc);
        }
    }
    catch (po::unknown_option &e) {
        std::cerr << e.what() << "; use --help for list of options\n";
        std::exit(1);
    }
    catch (std::logic_error &e) {
        std::cerr << "std::logic error: " << e.what() << "\n";
        std::exit(1);
    }
    catch (std::runtime_error &e) {
        std::cerr << "std::runtime error: " << e.what() << "\n";
        std::exit(1);
    }
    catch ( ... ) {
        std::cerr << "Unknown Exception" << std::endl;
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
