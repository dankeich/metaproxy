/* $Id: filter_auth_simple.hpp,v 1.2 2006-01-17 17:11:45 mike Exp $
   Copyright (c) 2005, Index Data.

%LICENSE%
 */

#ifndef FILTER_AUTH_SIMPLE_HPP
#define FILTER_AUTH_SIMPLE_HPP

#include <boost/scoped_ptr.hpp>

#include "filter.hpp"

namespace yp2 {
    namespace filter {
        class AuthSimple : public Base {
            class Rep;
            boost::scoped_ptr<Rep> m_p;
        public:
            AuthSimple();
            ~AuthSimple();
            void configure(const xmlNode * ptr);
            void process(yp2::Package & package) const;
        private:
            void process_init(yp2::Package & package) const;
            void process_search(yp2::Package & package) const;
        };
    }
}

extern "C" {
    extern struct yp2_filter_struct yp2_filter_auth_simple;
}

#endif
/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
