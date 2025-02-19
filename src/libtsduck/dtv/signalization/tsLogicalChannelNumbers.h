//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2023, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/#license
//
//----------------------------------------------------------------------------
//!
//!  @file
//!  A multi-standard storage of Logical Channel Numbers (LCN).
//!
//----------------------------------------------------------------------------

#pragma once
#include "tsDuckContext.h"
#include "tsService.h"
#include "tsServiceIdTriplet.h"
#include "tsAbstractLogicalChannelDescriptor.h"
#include "tsNIT.h"

namespace ts {
    //!
    //! A multi-standard storage of Logical Channel Numbers (LCN).
    //! @ingroup mpeg
    //!
    //! Logical Channel Numbers (LCN) are an important data for operators and users.
    //! However, there is not standard way to define them in the signalization.
    //! Several private descriptors exist. This class is a store of LCN values
    //! which can be collected from many type of tables.
    //!
    class TSDUCKDLL LogicalChannelNumbers
    {
        TS_NOBUILD_NOCOPY(LogicalChannelNumbers);
    public:
        //!
        //! Constructor.
        //! @param [in,out] duck TSDuck execution context.
        //!
        LogicalChannelNumbers(DuckContext& duck);

        //!
        //! Clear the content of the LCN store.
        //!
        void clear() { _lcn_map.clear(); }

        //!
        //! Check if the LCN store is empty.
        //! @return True if the LCN store is empty.
        //!
        bool empty() const { return _lcn_map.empty(); }

        //!
        //! Get the number of services in the LCN store.
        //! @return The number of services in the LCN store.
        //!
        size_t size() const { return _lcn_map.size(); }

        //!
        //! Add the logical channel number of a service.
        //! @param [in] lcn The logical channel number to add.
        //! @param [in] srv_id The service id.
        //! @param [in] ts_id The transport stream id.
        //! @param [in] onet_id The original network id. Use 0xFFFF for "unspecified".
        //! @param [in] visible The service LCN is visible.
        //!
        void addLCN(uint16_t lcn, uint16_t srv_id, uint16_t ts_id, uint16_t onet_id, bool visible = true);

        //!
        //! Collect all LCN which are declared in a NIT.
        //! @param [in] nit The NIT to analyze.
        //! @param [in] ts_id If not 0xFFFF, get services from that TS id only.
        //! @param [in] onet_id If not 0xFFFF, get services from that original network id only.
        //! @return The number of collected LCN.
        //!
        size_t addFromNIT(const NIT& nit, uint16_t ts_id = 0xFFFF, uint16_t onet_id = 0xFFFF);

        //!
        //! Collect all LCN which are declared in a list of descriptors.
        //! @param [in] descs The list of descriptors to analyze.
        //! @param [in] ts_id The transport stream id of all services.
        //! @param [in] onet_id The original network id to use.
        //! If set to 0xFFFF (the default), leave it unspecified.
        //! @return The number of collected LCN.
        //!
        size_t addFromDescriptors(const DescriptorList& descs, uint16_t ts_id, uint16_t onet_id = 0xFFFF);

        //!
        //! Get the logical channel number of a service.
        //! @param [in] srv_id The service id to search.
        //! @param [in] ts_id The transport stream id of the service.
        //! @param [in] onet_id The original network id of the service.
        //! If set to 0xFFFF (the default), the first match service is used.
        //! @return The LCN of the service or 0xFFFF if not found.
        //!
        uint16_t getLCN(uint16_t srv_id, uint16_t ts_id, uint16_t onet_id = 0xFFFF) const;

        //!
        //! Get the logical channel number of a service.
        //! @param [in] srv The service id triplet to search.
        //! @return The LCN of the service or 0xFFFF if not found.
        //!
        uint16_t getLCN(const ServiceIdTriplet& srv) const;

        //!
        //! Get all known services by logical channel number.
        //! @param [out] lcns A map of all known LCN's, key: LCN, value: service id triplet.
        //! @param [in] ts_id If not 0xFFFF, get services from that TS id only.
        //! @param [in] onet_id If not 0xFFFF, get services from that original network id only.
        //!
        void getLCNs(std::map<uint16_t,ServiceIdTriplet>& lcns, uint16_t ts_id = 0xFFFF, uint16_t onet_id = 0xFFFF) const;

        //!
        //! Get the visible flag of a service.
        //! @param [in] srv_id The service id to search.
        //! @param [in] ts_id The transport stream id of the service.
        //! @param [in] onet_id The original network id of the service.
        //! If set to 0xFFFF (the default), the first match service is used.
        //! @return The visible flag of the service or true if not found.
        //!
        bool getVisible(uint16_t srv_id, uint16_t ts_id, uint16_t onet_id = 0xFFFF) const;

        //!
        //! Get the visible flag of a service.
        //! @param [in] srv The service id triplet to search.
        //! @return The visible flag of the service or true if not found.
        //!
        bool getVisible(const ServiceIdTriplet& srv) const;

        //!
        //! Update a service description with its LCN.
        //! @param [in,out] srv The service description to update.
        //! The service id and transport stream id must be set. If the original network id is unset,
        //! the first LCN matching the service id and transport stream id is used.
        //! @param [in] replace If @a srv already has an LCN and @a replace is false, don't search.
        //! @return True if the LCN was updated, false otherwise.
        //!
        bool updateService(Service& srv, bool replace) const;

        //!
        //! Update a list of service descriptions with LCN's.
        //! @param [in,out] services The list of service description to update.
        //! @param [in] replace If a service already has an LCN and @a replace is false, don't search.
        //! @param [in] add If true, add in @a services all missing services for which an LCN is known.
        //! @return Number of updated or added LCN.
        //!
        size_t updateServices(ServiceList& services, bool replace, bool add) const;

    private:
        // Storage of one LCN, except the service id which is used as an index.
        class LCN
        {
        public:
            uint16_t lcn;      // Logical channel number.
            uint16_t ts_id;    // Transport stream id.
            uint16_t onet_id;  // Original network id, 0xFFFF means unspecified.
            bool     visible;  // Channel is visible.
        };

        // The LCN store is indexed by service id only. This is more efficient than using the DVB triplet as index.
        // This is a multimap since the same sevice id can be used on different TS.
        typedef std::multimap<uint16_t,LCN> LCNMap;

        // Get LCN entry.
        LCNMap::const_iterator findLCN(uint16_t srv_id, uint16_t ts_id, uint16_t onet_id) const;

        // Collect LCN for a generic form of LCN descriptor.
        size_t addFromAbstractLCN(const AbstractLogicalChannelDescriptor& desc, uint16_t ts_id, uint16_t onet_id);

        // LCN store private members.
        DuckContext& _duck;
        LCNMap _lcn_map {};
    };
}
