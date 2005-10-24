/* $Id: filter_virt_db.hpp,v 1.1 2005-10-24 14:33:30 adam Exp $
   Copyright (c) 2005, Index Data.

%LICENSE%
 */

#ifndef FILTER_VIRT_DB_HPP
#define FILTER_VIRT_DB_HPP

#include <stdexcept>
#include <list>

#include "filter.hpp"

namespace yp2 {
    namespace filter {
        class Virt_db : public Base {
            class Rep;
        public:
            ~Virt_db();
            Virt_db();
            void process(yp2::Package & package) const;
            void add_map_db2vhost(std::string db, std::string vhost);
        private:
            Rep *m_p;
        };
    }
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
