/* $Id: filter_dl.cpp,v 1.1 2005-12-10 09:59:10 adam Exp $
   Copyright (c) 2005, Index Data.

%LICENSE%
 */

#include "config.hpp"

#include "filter.hpp"
#include "router.hpp"
#include "package.hpp"

namespace yp2 {
    namespace filter {
        class Filter_dl: public yp2::filter::Base {
        public:
            void process(yp2::Package & package) const;
        };
    }
}

void yp2::filter::Filter_dl::process(yp2::Package & package) const
{
    package.data() = 42;
}

static yp2::filter::Base* filter_creator()
{
    return new yp2::filter::Filter_dl;
}

extern "C" {
    struct yp2_filter_struct yp2_filter_dl = {
        0,
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
