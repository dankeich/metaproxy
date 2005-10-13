#include "config.hpp"
#include "session.hpp"

#include <iostream>

#define BOOST_AUTO_TEST_MAIN
#include <boost/test/auto_unit_test.hpp>

using namespace boost::unit_test;

BOOST_AUTO_TEST_CASE( testsession1 ) 
{

    // test session 
    try {
        yp2::Session session1;
        yp2::Session session2;
        yp2::Session session3;
        yp2::Session session4;
        yp2::Session session5;

        BOOST_CHECK_EQUAL (session5.id(), 5);

        yp2::Session session = session3;

        BOOST_CHECK_EQUAL (session.id(), 3);
    }
    catch (std::exception &e) {
        std::cout << e.what() << "\n";
        BOOST_CHECK (false);
    }
    catch (...) {
        BOOST_CHECK (false);
    }
}

/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
