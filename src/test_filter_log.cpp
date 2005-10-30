/* $Id: test_filter_log.cpp,v 1.6 2005-10-30 17:13:36 adam Exp $
   Copyright (c) 2005, Index Data.

%LICENSE%
 */

#include "config.hpp"
#include <iostream>
#include <stdexcept>

#include "filter_log.hpp"
#include "util.hpp"
#include "router_chain.hpp"
#include "session.hpp"
#include "package.hpp"

#define BOOST_AUTO_TEST_MAIN
#include <boost/test/auto_unit_test.hpp>

using namespace boost::unit_test;

class FilterBounceInit: public yp2::filter::Base {
public:
    void process(yp2::Package & package) const {
        
        if (package.session().is_closed())
        {
            // std::cout << "Got Close.\n";
        }
       
        Z_GDU *gdu = package.request().get();
        if (gdu)
        {
            // std::cout << "Got PDU. Sending init response\n";
            yp2::odr odr;
            Z_APDU *apdu = zget_APDU(odr, Z_APDU_initResponse);
            
            apdu->u.initResponse->implementationName = "YP2/YAZ";
            
            package.response() = apdu;
        }
        return package.move();
    };
    const std::string type() const {
        return "FilterBounceInit";
    };
};


BOOST_AUTO_TEST_CASE( test_filter_log_1 )
{
    try 
    {
        yp2::filter::Log lf;
    }
    catch ( ... ) {
        BOOST_CHECK (false);
    }
}

BOOST_AUTO_TEST_CASE( test_filter_log_2 )
{
    try 
    {
        yp2::RouterChain router;
        
        yp2::filter::Log lf;
        FilterBounceInit bf;
        
        router.append(lf);
        router.append(bf);
        
        // Create package with Z39.50 init request in it
        yp2::Package pack;
        
        yp2::odr odr;
        Z_APDU *apdu = zget_APDU(odr, Z_APDU_initRequest);
        
        pack.request() = apdu;
        // Done creating query. 
        
        // Put it in router
        pack.router(router).move(); 
        
        // Inspect that we got Z39.50 init response
        yazpp_1::GDU *gdu = &pack.response();
        
        Z_GDU *z_gdu = gdu->get();
        BOOST_CHECK(z_gdu);
        if (z_gdu) {
            BOOST_CHECK_EQUAL(z_gdu->which, Z_GDU_Z3950);
            BOOST_CHECK_EQUAL(z_gdu->u.z3950->which, Z_APDU_initResponse);
        }
    }
    catch ( ... ) {
        BOOST_CHECK (false);
    }
}

/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
