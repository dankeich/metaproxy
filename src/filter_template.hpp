/* $Id: filter_template.hpp,v 1.5 2006-03-16 10:40:59 adam Exp $
   Copyright (c) 2005-2006, Index Data.

%LICENSE%
 */

// Filter that does nothing. Use as template for new filters 
#ifndef FILTER_TEMPLATE_HPP
#define FILTER_TEMPLATE_HPP

#include <boost/scoped_ptr.hpp>

#include "filter.hpp"

namespace metaproxy_1 {
    namespace filter {
        class Template : public Base {
            class Rep;
            boost::scoped_ptr<Rep> m_p;
        public:
            Template();
            ~Template();
            void process(metaproxy_1::Package & package) const;
        };
    }
}

extern "C" {
    extern struct metaproxy_1_filter_struct metaproxy_1_filter_template;
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
