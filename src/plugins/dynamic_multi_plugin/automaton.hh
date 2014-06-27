/**
 *   Copyright (C) 2012-2013 IFSTTAR (http://www.ifsttar.fr)
 *   Copyright (C) 2012-2013 Oslandia <infos@oslandia.com>
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Library General Public
 *   License as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Library General Public License for more details.
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/// Automaton representing forbidden sequences
///
/// It is represented by a graph (even if a tree should be enough)
/// Each node represents a automaton's state, each edge represents a possible transition.
/// A transition from a state s to a state s' is possible only in the presence of a given
/// road/multimodal vertex or edge, called a 'symbol'
///
/// The 'output' part of the automaton serves to represent penalties of road sequences
///
/// The automaton is build upon sequences of forbidden movements and penalties
/// The optimized graph (finite state machine) is constructed by following the Aho & Corasick algorithms

namespace Tempus {
	
template <class Symbol>	
class Automaton {
public:	
		
    struct Node {
        Road::Restriction::CostPerTransport penalty_per_mode;
    };
		
    struct Arc {
        // Symbol (vertex or edge of the main graph) associated with the automaton edge (or transition)
        Symbol symbol;
    };
		
    typedef boost::adjacency_list< boost::vecS,
                                   boost::vecS,
                                   boost::directedS,
                                   Node,
                                   Arc > Graph;
	
    /// Vertex of the graph, i.e. state of the automaton
    typedef typename Graph::vertex_descriptor State;
    /// Edge of the graph, i.e. transition
    typedef typename Graph::edge_descriptor Transition;
	
    class NodeWriter
    {
    public:
        NodeWriter( const Graph& agraph ) : agraph_(agraph) {}
        void operator() ( std::ostream& ostr, const State& v) const {
            ostr << "[label=\"" << v << " " ;
            for (Road::Restriction::CostPerTransport::const_iterator penaltyIt = agraph_[v].penalty_per_mode.begin();
                 penaltyIt != agraph_[v].penalty_per_mode.end() ;
                 penaltyIt++ ) {
                ostr << "(mode:" << penaltyIt->first << ", penalty:" << penaltyIt->second << ") " ;			
            } 			
            ostr << "\"]" ; 
        }
    private:
        const Graph& agraph_;
    }; 
		
    class ArcWriter
    {
    public:
        ArcWriter( const Graph& agraph, Tempus::Road::Graph& graph ) : agraph_(agraph), graph_(graph) {}
        void operator() ( std::ostream& ostr, const Transition& e) const {
            ostr << "[label=\"" << graph_[agraph_[e].symbol].db_id() << " = " << agraph_[e].symbol << "\"]"; 		
        }
    private:
        const Graph& agraph_;
        const Tempus::Road::Graph& graph_;
    }; 
		
    // Attributes 	
    Graph automaton_graph_;
    State initial_state_; 
	
    // Constructor
    Automaton() {}
		
    // Methods
    void build_graph( Road::Restrictions sequences )
    {
        automaton_graph_.clear(); 
			
        initial_state_ = boost::add_vertex( automaton_graph_ ); 
	
        for ( std::list<Road::Restriction>::const_iterator it=sequences.restrictions().begin();
              it!=sequences.restrictions().end();
              it++ ) {
            add_sequence(it->road_edges(), it->cost_per_transport()); 
        } 
	
        // The automaton graph is modified to represent the "next" function, the "failure" function is used to operate this transformation. 
        build_shortcuts( build_failure_function() );
    }

    ///
    /// Corresponds to the enter() function in Aho & al. paper, responsible for the creation of the "goto" function
    /// here represented as an automaton
    void add_sequence( const std::vector< Symbol >& symbol_sequence, const Road::Restriction::CostPerTransport& penalty_per_mode )
    {
        std::cout << "add sequence" << std::endl;
        State current_state = initial_state_ ;

        // Iterating over forbidden road sequences 
        for ( size_t j = 0; j < symbol_sequence.size(); j++ ) {
            std::pair< State, bool > transition_result = find_transition( current_state, symbol_sequence[j] );
				
            if ( transition_result.second == true )  {// this state already exists
                current_state = transition_result.first;
            }
            else { // this state needs to be inserted
                State s = boost::add_vertex( automaton_graph_ );
                Transition e;
                bool ok;
                boost::tie( e, ok ) = boost::add_edge( current_state, s, automaton_graph_ );
                BOOST_ASSERT( ok );
                automaton_graph_[e].symbol = symbol_sequence[j];
                if ( j == symbol_sequence.size()-1 ) {
                    automaton_graph_[ s ].penalty_per_mode = penalty_per_mode;
                }
                current_state = s; 
            } 
        }
        std::cout << "end add sequence" << std::endl;
    } 

    /// Build the failure function (see Aho & al.)
    std::map < State, State > build_failure_function()
    {
        std::cout << "failure function" << std::endl;
        std::map< State, State > failure_function_; 
        failure_function_.clear(); 
        std::list< State > q; 
				
        // All successors of the initial state return to initial state when failing
        BGL_FORALL_OUTEDGES_T( initial_state_, edge, automaton_graph_, Graph ) {
            const State& s = target( edge, automaton_graph_ );
            q.push_back( s );
            failure_function_[ s ] = initial_state_;
        } 
				
        // Iterating over the list of states which do not have failure state
        State s, r, state;
        while ( ! q.empty() ) {
            r = q.front();
            q.pop_front();
            BGL_FORALL_OUTEDGES_T( r, edge, automaton_graph_, Graph ) {
                s = target( edge, automaton_graph_ );
                q.push_back( s );
                Symbol a = automaton_graph_[edge].symbol;
                state = failure_function_[ s ];
                while ( find_transition( state, a ).second == false && state != initial_state_ ) {
                    state = failure_function_[ state ];
                }
                std::pair<State,bool> t = find_transition( state, a );
                if ( t.second == true ) {
                    failure_function_[ s ] = t.first;
                }
                else {
                    failure_function_[ s ] = initial_state_;
                }
            } 
        }

        std::cout << "end failure" << std::endl;
        return failure_function_;
    }

    /// corresponds to building the "next" function
    void build_shortcuts(const std::map < State, State >& failure_function_)
    {
        std::cout << "shortcurts" << std::endl;
        using namespace boost;
        std::list< State > q;
			
        std::cout << "first push back" << std::endl;
        BGL_FORALL_OUTEDGES_T( initial_state_, edge, automaton_graph_, Graph ) {
            BOOST_ASSERT( edge.get_property() );
            q.push_back( target( edge, automaton_graph_ ) );
        }
        std::cout << "end first push back" << std::endl;

        State r; 
        while ( ! q.empty() ) {
            r = q.front();
            q.pop_front();

            State f = initial_state_;
            typename std::map<State,State>::const_iterator fit = failure_function_.find(r);
            BOOST_ASSERT( fit != failure_function_.end() );
            f = fit->second;
            if ( f != initial_state_ ) {
                BOOST_ASSERT( f != r ); // f != r, unless the failure function is not built correctly

                // We are going to modify the graph (add_edge, remove_edge)
                // we cannot do it during an iteration over out edges
                // (because we are using an adjacency_list with boost::vecS for edges)
                // they then must be stored to avoid undefined behaviour
                std::vector< std::pair< State, Symbol > > oedges;
                BGL_FORALL_OUTEDGES_T( f, edge, automaton_graph_, Graph ) {
                    oedges.push_back( std::make_pair( target( edge, automaton_graph_ ),
                                                      automaton_graph_[edge].symbol ) );
                }
                for ( size_t i = 0; i < oedges.size(); i++ ) {
                    const State& v = oedges[i].first;
                    const Symbol& symbol = oedges[i].second;
                    // if a transition from r with this symbol already exists, remove it
                    std::pair<State, bool> t = find_transition( f, symbol );
                    if ( t.second == true ) {
                        // a transition already exists, first remove it
                        remove_edge( f, t.first, automaton_graph_ );
                    }
                    Transition e;
                    bool ok;
                    tie( e, ok ) = add_edge( r, v, automaton_graph_ );
                    BOOST_ASSERT( ok );
                    automaton_graph_[ e ].symbol = symbol;
                }
            }
            BGL_FORALL_OUTEDGES_T( r, edge, automaton_graph_, Graph ) {
                q.push_back( target( edge, automaton_graph_ ) );
            }
        }
        std::cout << "end shortcuts" << std::endl;
    }
		
    ///
    /// Look for a transition in the automaton from the state q
    /// and involving an symbol (vertex or edge) e
    std::pair< State, bool > find_transition( State q, Symbol e ) const
    {
        BGL_FORALL_OUTEDGES_T( q, edge, automaton_graph_, Graph ) {
            if ( automaton_graph_[ edge ].symbol == e ) {
                return std::make_pair( target( edge, automaton_graph_ ), true );
            }
        }
        // If transition not found
        return std::make_pair( q, false );
    }
};

} // namespace Tempus

