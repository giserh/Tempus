#include "osm2tempus.h"
#include "writer.h"

#include <unordered_set>

struct PbfReaderPass1
{
public:
    void node_callback( uint64_t osmid, double lon, double lat, const osm_pbf::Tags &/*tags*/ )
    {
        points_[osmid] = Point(lon, lat);
    }

    void way_callback( uint64_t /*osmid*/, const osm_pbf::Tags& tags, const std::vector<uint64_t>& nodes )
    {
        // ignore ways that are not highway
        if ( tags.find( "highway" ) == tags.end() )
            return;

        for ( uint64_t node: nodes ) {
            auto it = points_.find( node );
            if ( it != points_.end() ) {
                int uses = it->second.uses();
                if ( uses < 2 )
                    it->second.set_uses( uses + 1 );
            }
        }
    }
    void relation_callback( uint64_t /*osmid*/, const osm_pbf::Tags &/*tags*/, const osm_pbf::References & /*refs*/ )
    {
    }

    PointCache& points() { return points_; }
private:
    PointCache points_;
};

struct PbfReaderPass2
{
    PbfReaderPass2( PointCache& points, Writer& writer ):
        points_( points ), writer_( writer )
    {}
    
    void node_callback( uint64_t /*osmid*/, double /*lon*/, double /*lat*/, const osm_pbf::Tags &/*tags*/ )
    {
    }

    void way_callback( uint64_t /*osmid*/, const osm_pbf::Tags& tags, const std::vector<uint64_t>& nodes )
    {
        // ignore ways that are not highway
        if ( tags.find( "highway" ) == tags.end() )
            return;

        // split the way on intersections (i.e. node that are used more than once)
        bool section_start = true;
        uint64_t old_node = nodes[0];
        if ( points_.find( old_node ) == points_.end() )
            return;
        uint64_t node_from;
        std::vector<uint64_t> section_nodes;
        for ( size_t i = 1; i < nodes.size(); i ++ ) {
            uint64_t node = nodes[i];
            auto it = points_.find( node );
            if ( it == points_.end() ) {
                // ignore ways with unknown nodes
                section_start = true;
                continue;
            }

            const Point& pt = it->second;
            if ( section_start ) {
                section_nodes.clear();
                section_nodes.push_back( old_node );
                node_from = old_node;
                section_start = false;
            }
            section_nodes.push_back( node );
            if ( i == nodes.size() - 1 || pt.uses() > 1 ) {
                split_into_sections( node_from, node, section_nodes, tags );
                section_start = true;
            }
            old_node = node;
        }
    }

    void relation_callback( uint64_t /*osmid*/, const osm_pbf::Tags &/*tags*/, const osm_pbf::References & /*refs*/ )
    {
    }

private:
    PointCache& points_;

    Writer& writer_;

    // structure used to detect multi edges
    std::unordered_set<node_pair> way_node_pairs;
    //std::unordered_map<node_pair, uint64_t> way_node_pairs;
    uint64_t way_id = 0;

    // node ids that are introduced to split multi edges
    // we count them backward from 2^64 - 1
    // this should not overlap current OSM node ID (~ 2^32 in july 2016)
    uint64_t last_artificial_node_id = 0xFFFFFFFFFFFFFFFFLL;

    ///
    /// Check if a section with the same (from, to) pair exists
    /// In which case, it is split into two sections
    /// in order to avoid multigraphs
    void split_into_sections( uint64_t node_from, uint64_t node_to, const std::vector<uint64_t>& nodes, const osm_pbf::Tags& tags )
    {
        node_pair p ( node_from, node_to );
        if ( way_node_pairs.find( p ) != way_node_pairs.end() ) {
            // split the way
            // if there are more than two nodes, just split on a node
            std::vector<Point> before_pts, after_pts;
            uint64_t center_node;
            if ( nodes.size() > 2 ) {
                size_t center = nodes.size() / 2;
                center_node = nodes[center];
                for ( size_t i = 0; i <= center; i++ ) {
                    before_pts.push_back( points_.find( nodes[i] )->second );
                }
                for ( size_t i = center; i < nodes.size(); i++ ) {
                    after_pts.push_back( points_.find( nodes[i] )->second );
                }
            }
            else {
                const Point& p1 = points_.find( nodes[0] )->second;
                const Point& p2 = points_.find( nodes[1] )->second;
                Point center_point( ( p1.lon() + p2.lon() ) / 2.0, ( p1.lat() + p2.lat() ) / 2.0 );
                
                before_pts.push_back( points_.find( nodes[0] )->second );
                before_pts.push_back( center_point );
                after_pts.push_back( center_point );
                after_pts.push_back( points_.find( nodes[1] )->second );

                // add a new point
                center_node = last_artificial_node_id;
                points_[last_artificial_node_id--] = center_point;
            }
            writer_.write_section( node_from, center_node, before_pts, tags );
            writer_.write_section( center_node, node_to, after_pts, tags );
        }
        else {
            way_node_pairs.insert( p );
            std::vector<Point> section_pts;
            for ( uint64_t node: nodes ) {
                section_pts.push_back( points_.find( node )->second );
            }
            writer_.write_section( node_from, node_to, section_pts, tags );
        }
    }
};

void two_pass_pbf_read( const std::string& filename, Writer& writer )
{
    std::cout << "first pass" << std::endl;
    PbfReaderPass1 p1;
    osm_pbf::read_osm_pbf<PbfReaderPass1, StdOutProgressor>( filename, p1 );
    std::cout << "second pass" << std::endl;
    PbfReaderPass2 p2( p1.points(), writer );
    osm_pbf::read_osm_pbf<PbfReaderPass2, StdOutProgressor>( filename, p2 );
}









