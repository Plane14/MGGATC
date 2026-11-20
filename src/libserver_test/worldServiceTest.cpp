//
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
//

#include <memory>
#include <chrono>
#include <functional>
#include "gtest/gtest.h"
#include "libworld.h"
#include "libworld_test.h"
#include "worldService.hpp"

using namespace std;
using namespace world;
using namespace server;

namespace server_test
{
    // minimal airport: runway 09/27, gate G1, taxi chain N1-A-N2-B-N3, plus disconnected N4-C-N5
    static shared_ptr<Airport> createTestAirport(shared_ptr<TestHostServices> host)
    {
        auto n1 = shared_ptr<TaxiNode>(new TaxiNode(1, UniPoint::fromGeo(host, {30.10, 45.10})));
        auto n2 = shared_ptr<TaxiNode>(new TaxiNode(2, UniPoint::fromGeo(host, {30.10, 45.20})));
        auto n3 = shared_ptr<TaxiNode>(new TaxiNode(3, UniPoint::fromGeo(host, {30.10, 45.30})));
        auto n4 = shared_ptr<TaxiNode>(new TaxiNode(4, UniPoint::fromGeo(host, {30.40, 45.10})));
        auto n5 = shared_ptr<TaxiNode>(new TaxiNode(5, UniPoint::fromGeo(host, {30.40, 45.20})));

        auto e12 = shared_ptr<TaxiEdge>(new TaxiEdge(12, "A", 1, 2));
        auto e23 = shared_ptr<TaxiEdge>(new TaxiEdge(23, "B", 2, 3));
        auto e45 = shared_ptr<TaxiEdge>(new TaxiEdge(45, "C", 4, 5));

        auto rwy = shared_ptr<Runway>(new Runway(
            Runway::End("09", 0, 0, n1->location()),
            Runway::End("27", 0, 0, n3->location()),
            30
        ));

        auto g1 = make_shared<ParkingStand>(
            1001, "G1", ParkingStand::Type::Gate, UniPoint::fromGeo(host, {30.20, 45.20}), 0, "1");

        return WorldBuilder::assembleAirport(
            host,
            Airport::Header("EFGH", "Test Airport", GeoPoint(30, 40), 123),
            { rwy },
            { g1 },
            { n1, n2, n3, n4, n5 },
            { e12, e23, e45 }
        );
    }

    static shared_ptr<TestHostServices> createHost()
    {
        return TestHostServices::createWithWorldAirports({ createTestAirport });
    }

    TEST(worldServiceTest, connect_good_token_replies_banner)
    {
        auto host = createHost();
        WorldService service(host);

        world_proto::ClientToServer_Connect request;
        request.set_token("HELLO");
        world_proto::ServerToClient reply;
        service.connect(request, reply);

        EXPECT_EQ(reply.payload_case(), world_proto::ServerToClient::kReplyConnect);
        EXPECT_EQ(reply.reply_connect().server_banner(), "AT&C plugin");
    }

    TEST(worldServiceTest, connect_bad_token_is_declined)
    {
        auto host = createHost();
        WorldService service(host);

        world_proto::ClientToServer_Connect request;
        request.set_token("WRONG");
        world_proto::ServerToClient reply;
        service.connect(request, reply);

        EXPECT_EQ(reply.payload_case(), world_proto::ServerToClient::kFaultDeclined);
    }

    TEST(worldServiceTest, query_airport_found_replies_with_real_location_and_layout)
    {
        auto host = createHost();
        WorldService service(host);

        world_proto::ClientToServer_QueryAirport request;
        request.set_icao_code("EFGH");
        world_proto::ServerToClient reply;
        service.queryAirport(request, reply);

        ASSERT_EQ(reply.payload_case(), world_proto::ServerToClient::kReplyQueryAirport);
        const auto& airport = reply.reply_query_airport().airport();
        EXPECT_EQ(airport.icao(), "EFGH");
        EXPECT_DOUBLE_EQ(airport.location().lat(), 30);  // real datum, not hardcoded
        EXPECT_DOUBLE_EQ(airport.location().lon(), 40);
        EXPECT_EQ(airport.runways_size(), 1);
        EXPECT_EQ(airport.parking_stands_size(), 1);
        EXPECT_EQ(airport.taxi_nodes_size(), 5);
        EXPECT_EQ(airport.taxi_edges_size(), 3);
    }

    TEST(worldServiceTest, query_airport_unknown_icao_is_not_found)
    {
        auto host = createHost();
        WorldService service(host);

        world_proto::ClientToServer_QueryAirport request;
        request.set_icao_code("XXXX");
        world_proto::ServerToClient reply;
        service.queryAirport(request, reply);

        EXPECT_EQ(reply.payload_case(), world_proto::ServerToClient::kFaultNotFound);
    }

    TEST(worldServiceTest, query_taxi_path_found_replies_edge_ids)
    {
        auto host = createHost();
        WorldService service(host);

        world_proto::ClientToServer_QueryTaxiPath request;
        request.set_airport_icao("EFGH");
        request.mutable_from_point()->set_lat(30.10);
        request.mutable_from_point()->set_lon(45.10);
        request.mutable_to_point()->set_lat(30.10);
        request.mutable_to_point()->set_lon(45.30);
        world_proto::ServerToClient reply;
        service.queryTaxiPath(request, reply);

        ASSERT_EQ(reply.payload_case(), world_proto::ServerToClient::kReplyQueryTaxiPath);
        const auto& path = reply.reply_query_taxi_path().taxi_path();
        EXPECT_EQ(path.from_node_id(), 1);
        EXPECT_EQ(path.to_node_id(), 3);
        ASSERT_EQ(path.edge_ids_size(), 2);
        EXPECT_EQ(path.edge_ids(0), 12);
        EXPECT_EQ(path.edge_ids(1), 23);
    }

    TEST(worldServiceTest, query_taxi_path_unknown_airport_is_not_found_not_crash)
    {
        auto host = createHost();
        WorldService service(host);

        world_proto::ClientToServer_QueryTaxiPath request;
        request.set_airport_icao("XXXX");
        request.mutable_from_point()->set_lat(30.10);
        request.mutable_from_point()->set_lon(45.10);
        request.mutable_to_point()->set_lat(30.10);
        request.mutable_to_point()->set_lon(45.30);
        world_proto::ServerToClient reply;
        service.queryTaxiPath(request, reply); // must not dereference null airport

        EXPECT_EQ(reply.payload_case(), world_proto::ServerToClient::kFaultNotFound);
    }

    TEST(worldServiceTest, query_taxi_path_disconnected_nodes_is_not_found)
    {
        auto host = createHost();
        WorldService service(host);

        world_proto::ClientToServer_QueryTaxiPath request;
        request.set_airport_icao("EFGH");
        request.mutable_from_point()->set_lat(30.10);
        request.mutable_from_point()->set_lon(45.10); // near N1
        request.mutable_to_point()->set_lat(30.40);
        request.mutable_to_point()->set_lon(45.10);   // near N4, different component
        world_proto::ServerToClient reply;
        service.queryTaxiPath(request, reply);

        EXPECT_EQ(reply.payload_case(), world_proto::ServerToClient::kFaultNotFound);
    }
}
