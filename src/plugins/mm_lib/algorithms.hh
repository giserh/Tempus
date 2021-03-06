/**
 *   Copyright (C) 2012-2013 IFSTTAR (http://www.ifsttar.fr)
 *   Copyright (C) 2012-2013 Oslandia <infos@oslandia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// Path algorithms for combined graphs with homogen labeling strategy

#ifdef _WIN32
#pragma warning(push, 0)
#endif
#include <boost/pending/indirect_cmp.hpp>
#include <boost/heap/d_ary_heap.hpp>
#include <boost/heap/binomial_heap.hpp>
#ifdef _WIN32
#pragma warning(pop)
#endif

#include <type_traits>

#include "reverse_multimodal_graph.hh"

namespace Tempus {

struct NullHeuristic
{
    float operator() ( const Multimodal::Vertex& ) const
    {
        return 0.0;
    }
};

template <class Object, class VertexDataMap, class Heuristic >
struct HeuristicCompare
{
    VertexDataMap pmap_;
    Heuristic h_;
    HeuristicCompare( const VertexDataMap& pmap, Heuristic h ) : pmap_(pmap), h_(h) {}
    bool operator()( const Object& a, const Object& b ) const {
        return (get( pmap_, a ).potential() + h_(a.vertex)) > (get( pmap_, b ).potential() + h_(b.vertex));
    }
};

//
// Implementation of the Dijkstra algorithm (label-setting) for a graph and an automaton
template < class NetworkGraph,
           class Automaton,
           class Object, 
           class VertexDataMap,
           class Visitor,
           class CostCalculator>
void combined_ls_algorithm_no_init(
                                   const NetworkGraph& graph,
                                   const Automaton& automaton,
                                   Object source_object,
                                   VertexDataMap vertex_data_map,
                                   CostCalculator cost_calculator, 
                                   const std::vector<db_id_t>& request_allowed_modes,
                                   Visitor vis,
                                   std::function<double (const Multimodal::Vertex&)> heuristic = NullHeuristic() )
{
    using VertexData = typename boost::property_traits<VertexDataMap>::value_type;

    static_assert( std::is_same<typename boost::property_traits<VertexDataMap>::key_type, Object>::value, "The key type of the vertex data map must be the same as the type of the source" );

    typedef HeuristicCompare<Object, VertexDataMap, std::function<double (const Multimodal::Vertex&)> > Cmp;
    Cmp cmp( vertex_data_map, heuristic );

    typedef boost::heap::d_ary_heap< Object, boost::heap::arity<4>, boost::heap::compare< Cmp >, boost::heap::mutable_<true> > VertexQueue;
    VertexQueue vertex_queue( cmp ); 
    vertex_queue.push( source_object ); 
    vis.discover_vertex( source_object, graph );

    Object min_object; 

    // get transport mode objet for each allowed mode
    std::vector<TransportMode> modes;
    for ( size_t i = 0; i < request_allowed_modes.size(); i++ )
    {
        boost::optional<TransportMode> mode = graph.transport_mode( request_allowed_modes[i] );
        BOOST_ASSERT( mode );
        modes.push_back( *mode );
    }

    while ( !vertex_queue.empty() ) {
        min_object = vertex_queue.top();
        vis.examine_vertex( min_object, graph );
        vertex_queue.pop();

        const VertexData& min_vd = get( vertex_data_map, min_object );
        double min_pi = min_vd.potential();
        db_id_t initial_trip_id = min_vd.trip();
			
        const TransportMode& initial_mode = *graph.transport_mode( min_object.mode );

        BGL_FORALL_OUTEDGES_T( min_object.vertex, current_edge, graph, NetworkGraph ) {
            vis.examine_edge( current_edge, graph );

            typename Automaton::State s;
            {
                bool found;
                boost::tie( s, found ) = automaton.find_transition( min_object.state, current_edge.road_edge() );
                // if not found, s == 0
            }

            Object new_object;
            new_object.vertex = target(current_edge, graph);
            new_object.state = s;

            for ( size_t i = 0; i < modes.size(); i++ )
            {
                const TransportMode& mode = modes[i];

                // if this mode is not allowed on the current edge, skip it
                if ( ! (current_edge.traffic_rules() & mode.traffic_rules() ) ) {
                    vis.edge_not_relaxed( current_edge, mode.db_id(), graph );
                    continue;
                }

                new_object.mode = mode.db_id();

                VertexData new_vd = get( vertex_data_map, new_object );
                double new_pi = new_vd.potential();
                // don't forget to set to 0
                db_id_t final_trip_id = 0;
                double wait_time = 0.0;
                double initial_shift_time = 0.0;
                double final_shift_time = 0.0;

                // compute the time needed to transfer from one mode to another
                double cost = cost_calculator.transfer_time( current_edge, initial_mode, mode );
                if ( cost < std::numeric_limits<double>::max() )
                {
                    initial_shift_time = min_vd.shift_time();
                    // will update final_trip_id and wait_time
                    double travel_time = cost_calculator.travel_time( current_edge,
                                                                      mode.db_id(),
                                                                      min_pi,
                                                                      initial_shift_time,
                                                                      final_shift_time,
                                                                      initial_trip_id,
                                                                      final_trip_id,
                                                                      wait_time );
                    cost += travel_time;
                    if ( ( cost < std::numeric_limits<double>::max() ) && ( s != min_object.state ) ) {
                        cost += penalty( automaton.automaton_graph_, s, mode.traffic_rules() ) ;
                    }
                }

                if ( ( cost < std::numeric_limits<double>::max() ) && ( min_pi + cost < new_pi ) ) {
                    vis.edge_relaxed( current_edge, mode.db_id(), graph ); 

                    new_vd.set_potential( min_pi + cost );
                    new_vd.set_predecessor( min_object );
                    new_vd.set_trip( final_trip_id );
                    new_vd.set_wait_time( wait_time );
                    new_vd.set_shift_time( final_shift_time );
                    put( vertex_data_map, new_object, new_vd );

                    vertex_queue.push( new_object ); 
                    vis.discover_vertex( new_object, graph );
                }
                else {
                    vis.edge_not_relaxed( current_edge, mode.db_id(), graph );
                }
            }
        }

        vis.finish_vertex( min_object, graph ); 
    }
}
	
//
// Implementation of the Dijkstra algorithm (label-setting) for a multimodal graph
template < class Graph,
           class Object, 
           class VertexDataMap,
           class Visitor,
           class CostCalculator>
void combined_ls_algorithm_no_init(
                                   const Graph& graph,
                                   std::vector<Object> sources,
                                   VertexDataMap vertex_data_map,
                                   CostCalculator cost_calculator, 
                                   const std::vector<db_id_t>& request_allowed_modes,
                                   Visitor vis,
                                   std::function<double (const Multimodal::Vertex&)> heuristic = NullHeuristic() )
{
    using VertexData = typename boost::property_traits<VertexDataMap>::value_type;

    static_assert( std::is_same<typename boost::property_traits<VertexDataMap>::key_type, Object>::value, "The key type of the vertex data map must be the same as the type of the source" );

    typedef HeuristicCompare<Object, VertexDataMap, std::function<double (const Multimodal::Vertex&)> > Cmp;
    Cmp cmp( vertex_data_map, heuristic );

    typedef boost::heap::d_ary_heap< Object, boost::heap::arity<4>, boost::heap::compare< Cmp >, boost::heap::mutable_<true> > VertexQueue;
    VertexQueue vertex_queue( cmp );
    for ( const auto& s: sources ) {
        vertex_queue.push( s ); 
        vis.discover_vertex( s, graph );
    }

    Object min_object; 

    // get transport mode objet for each allowed mode
    std::vector<TransportMode> modes;
    for ( size_t i = 0; i < request_allowed_modes.size(); i++ )
    {
        boost::optional<TransportMode> mode = graph.transport_mode( request_allowed_modes[i] );
        BOOST_ASSERT( mode );
        modes.push_back( *mode );
    }

    while ( !vertex_queue.empty() ) {
        min_object = vertex_queue.top();
        vis.examine_vertex( min_object, graph );
        vertex_queue.pop();

        const VertexData& min_vd = get( vertex_data_map, min_object );
        double min_pi = min_vd.potential();
        db_id_t initial_trip_id = min_vd.trip();
			
        const TransportMode& initial_mode = *graph.transport_mode( min_object.mode );

        BGL_FORALL_OUTEDGES_T( min_object.vertex, current_edge, graph, Graph ) {
            vis.examine_edge( current_edge, graph );

            Object new_object;
            new_object.vertex = target(current_edge, graph);

            for ( size_t i = 0; i < modes.size(); i++ )
            {
                const TransportMode& mode = modes[i];

                // if this mode is not allowed on the current edge, skip it
                if ( ! (current_edge.traffic_rules() & mode.traffic_rules() ) ) {
                    vis.edge_not_relaxed( current_edge, mode.db_id(), graph );
                    continue;
                }

                new_object.mode = mode.db_id();

                VertexData new_vd = get( vertex_data_map, new_object );
                double new_pi = new_vd.potential();
                // don't forget to set to 0
                db_id_t final_trip_id = 0;
                double wait_time = 0.0;
                double initial_shift_time = 0.0;
                double final_shift_time = 0.0;

                // compute the time needed to transfer from one mode to another
                double cost = cost_calculator.transfer_time( current_edge, initial_mode, mode );
                if ( cost < std::numeric_limits<double>::max() )
                {
                    initial_shift_time = min_vd.shift_time();
                    // will update final_trip_id and wait_time
                    double travel_time = cost_calculator.travel_time( current_edge,
                                                                      mode.db_id(),
                                                                      min_pi,
                                                                      initial_shift_time,
                                                                      final_shift_time,
                                                                      initial_trip_id,
                                                                      final_trip_id,
                                                                      wait_time );
                    cost += travel_time;
                }

                if ( ( cost < std::numeric_limits<double>::max() ) && ( min_pi + cost < new_pi ) ) {
                    vis.edge_relaxed( current_edge, mode.db_id(), graph ); 

                    new_vd.set_potential( min_pi + cost );
                    new_vd.set_predecessor( min_object );
                    new_vd.set_trip( final_trip_id );
                    new_vd.set_wait_time( wait_time );
                    new_vd.set_shift_time( final_shift_time );
                    put( vertex_data_map, new_object, new_vd );

                    vertex_queue.push( new_object ); 
                    vis.discover_vertex( new_object, graph );
                }
                else {
                    vis.edge_not_relaxed( current_edge, mode.db_id(), graph );
                }
            }
        }

        vis.finish_vertex( min_object, graph ); 
    }
}

}// end namespace
