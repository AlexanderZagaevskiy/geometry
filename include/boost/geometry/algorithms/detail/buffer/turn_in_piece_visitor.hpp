// Boost.Geometry (aka GGL, Generic Geometry Library)

// Copyright (c) 2012-2014 Barend Gehrels, Amsterdam, the Netherlands.

// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_GEOMETRY_ALGORITHMS_DETAIL_BUFFER_TURN_IN_PIECE_VISITOR
#define BOOST_GEOMETRY_ALGORITHMS_DETAIL_BUFFER_TURN_IN_PIECE_VISITOR

#include <boost/range.hpp>

#include <boost/geometry/arithmetic/dot_product.hpp>
#include <boost/geometry/algorithms/assign.hpp>
#include <boost/geometry/algorithms/comparable_distance.hpp>
#include <boost/geometry/algorithms/equals.hpp>
#include <boost/geometry/algorithms/expand.hpp>
#include <boost/geometry/algorithms/detail/disjoint/point_box.hpp>
#include <boost/geometry/algorithms/detail/overlay/segment_identifier.hpp>
#include <boost/geometry/algorithms/detail/overlay/get_turn_info.hpp>
#include <boost/geometry/policies/compare.hpp>
#include <boost/geometry/strategies/buffer.hpp>


namespace boost { namespace geometry
{


#ifndef DOXYGEN_NO_DETAIL
namespace detail { namespace buffer
{

struct piece_get_box
{
    template <typename Box, typename Piece>
    static inline void apply(Box& total, Piece const& piece)
    {
        geometry::expand(total, piece.robust_envelope);
    }
};

struct piece_ovelaps_box
{
    template <typename Box, typename Piece>
    static inline bool apply(Box const& box, Piece const& piece)
    {
        return ! geometry::detail::disjoint::disjoint_box_box(box, piece.robust_envelope);
    }
};

struct turn_get_box
{
    template <typename Box, typename Turn>
    static inline void apply(Box& total, Turn const& turn)
    {
        geometry::expand(total, turn.robust_point);
    }
};

struct turn_ovelaps_box
{
    template <typename Box, typename Turn>
    static inline bool apply(Box const& box, Turn const& turn)
    {
        return ! dispatch::disjoint
            <
                typename Turn::robust_point_type,
                Box
            >::apply(turn.robust_point, box);
    }
};


enum analyse_result
{
    analyse_unknown,
    analyse_continue,
    analyse_disjoint,
    analyse_within,
    analyse_on_original_boundary,
    analyse_on_offsetted,
    analyse_near_offsetted
};

class analyse_turn_wrt_piece
{
    template <typename Point>
    static inline analyse_result check_segment(Point const& previous, Point const& current, Point const& point)
    {
        typedef typename strategy::side::services::default_strategy
            <
                typename cs_tag<Point>::type
            >::type side_strategy;
        typedef typename geometry::coordinate_type<Point>::type coordinate_type;
        typedef geometry::model::box<Point> box_type;


        // Get its box (TODO: this can be prepared-on-demand later)
        box_type box;
        geometry::assign_inverse(box);
        geometry::expand(box, previous);
        geometry::expand(box, current);

        coordinate_type const twice_area
            = side_strategy::template side_value
                <
                    coordinate_type,
                    coordinate_type
                >(previous, current, point);

        if (twice_area == 0)
        {
            // Collinear, only on segment if it is covered by its bbox
            if (geometry::covered_by(point, box))
            {
                return analyse_on_offsetted;
            }
        }

        if (twice_area < 0 && geometry::covered_by(point, box))
        {
            // It is in the triangle right-of the segment where the
            // segment is the hypothenusa. Check if it is close
            // (within rounding-area)
            if (twice_area * twice_area < geometry::comparable_distance(previous, current))
            {
                return analyse_near_offsetted;
            }
        }
//        if (twice_area > 0)
//        {
//            // Left of segment
//            // TODO: use within state here
//        }
        return analyse_continue;
    }

    template <typename Point>
    static inline analyse_result check_helper_segment(Point const& s1,
                Point const& s2, Point const& point,
                bool is_original,
                Point const& offsetted)
    {
        typedef typename strategy::side::services::default_strategy
            <
                typename cs_tag<Point>::type
            >::type side_strategy;

        switch(side_strategy::apply(s1, s2, point))
        {
            case 1 :
                return analyse_disjoint; // left of segment
            case 0 :
                {
                    // If is collinear, either on segment or before/after
                    typedef geometry::model::box<Point> box_type;

                    box_type box;
                    geometry::assign_inverse(box);
                    geometry::expand(box, s1);
                    geometry::expand(box, s2);

                    if (geometry::covered_by(point, box))
                    {
                        // It is on the segment
                        if (! is_original
                            && geometry::comparable_distance(point, offsetted) <= 1)
                        {
                            // It is close to the offsetted-boundary, take
                            // any rounding-issues into account
                            return analyse_near_offsetted;
                        }

                        // Points on helper-segments are considered as within
                        // Points on original boundary are processed differently
                        return is_original
                            ? analyse_on_original_boundary
                            : analyse_within;
                    }

                    // It is collinear but not on the segment. Because these
                    // segments are convex, it is outside
                    // Unless the offsetted ring is collinear or concave w.r.t.
                    // helper-segment but that scenario is not yet supported
                    return analyse_disjoint;
                }
                break;
        }

        // right of segment
        return analyse_continue;
    }

    template <typename Point, typename Piece>
    static inline analyse_result check_helper_segments(Point const& point, Piece const& piece)
    {
        geometry::equal_to<Point> comparator;

        Point points[4];

        int helper_count = piece.robust_ring.size() - piece.offsetted_count;
        if (helper_count == 4)
        {
            for (int i = 0; i < 4; i++)
            {
                points[i] = piece.robust_ring[piece.offsetted_count + i];
            }
        }
        else if (helper_count == 3)
        {
            // Triangular piece, assign points but assign second twice
            for (int i = 0; i < 4; i++)
            {
                int index = i < 2 ? i : i - 1;
                points[i] = piece.robust_ring[piece.offsetted_count + index];
            }
        }
        else
        {
            // Some pieces (e.g. around points) do not have helper segments.
            // Others should have 3 (join) or 4 (side)
            return analyse_continue;
        }

        // First check point-equality
        if (comparator(point, points[0]) || comparator(point, points[3]))
        {
            return analyse_on_offsetted;
        }
        if (comparator(point, points[1]) || comparator(point, points[2]))
        {
            return analyse_on_original_boundary;
        }

        // Right side of the piece
        analyse_result result
            = check_helper_segment(points[0], points[1], point,
                    false, points[0]);
        if (result != analyse_continue)
        {
            return result;
        }

        // Left side of the piece
        result = check_helper_segment(points[2], points[3], point,
                    false, points[3]);
        if (result != analyse_continue)
        {
            return result;
        }

        if (! comparator(points[1], points[2]))
        {
            // Side of the piece at side of original geometry
            result = check_helper_segment(points[1], points[2], point,
                        true, point);
            if (result != analyse_continue)
            {
                return result;
            }
        }

        // We are within the \/ or |_| shaped piece, where the top is the
        // offsetted ring.
        if (! geometry::covered_by(point, piece.robust_offsetted_envelope))
        {
            // Not in offsetted-area. This makes a cheap check possible
            typedef typename strategy::side::services::default_strategy
                <
                    typename cs_tag<Point>::type
                >::type side_strategy;

            switch(side_strategy::apply(points[3], points[0], point))
            {
                case 1 : return analyse_disjoint;
                case -1 : return analyse_within;
                case 0 : return analyse_disjoint;
            }
        }

        return analyse_continue;
    }

public :
    template <typename Point, typename Piece>
    static inline analyse_result apply(Point const& point, Piece const& piece)
    {
        analyse_result code = check_helper_segments(point, piece);
        if (code != analyse_continue)
        {
            return code;
        }

        geometry::equal_to<Point> comparator;

        for (int i = 1; i < piece.offsetted_count; i++)
        {
            Point const& previous = piece.robust_ring[i - 1];
            Point const& current = piece.robust_ring[i];

            // The robust ring can contain duplicates
            // (on which any side or side-value would return 0)
            if (! comparator(previous, current))
            {
                code = check_segment(previous, current, point);
                if (code != analyse_continue)
                {
                    return code;
                }
            }
        }

         return analyse_unknown;
    }

};


template <typename Turns, typename Pieces>
class turn_in_piece_visitor
{
    Turns& m_turns; // because partition is currently operating on const input only
    Pieces const& m_pieces; // to check for piece-type

public:

    inline turn_in_piece_visitor(Turns& turns, Pieces const& pieces)
        : m_turns(turns)
        , m_pieces(pieces)
    {}

    template <typename Turn, typename Piece>
    inline void apply(Turn const& turn, Piece const& piece, bool first = true)
    {
        boost::ignore_unused_variable_warning(first);

        if (turn.count_within > 0)
        {
            // Already inside - no need to check again
            return;
        }

        if (piece.type == strategy::buffer::buffered_flat_end
            || piece.type == strategy::buffer::buffered_concave)
        {
            // Turns cannot be inside a flat end (though they can be on border)
            // Neither we need to check if they are inside concave helper pieces
            return;
        }

        if (! geometry::covered_by(turn.robust_point, piece.robust_envelope))
        {
            // Easy check: if the turn is not in the envelope, we can safely return
            return;
        }

        bool neighbour = false;
        for (int i = 0; i < 2; i++)
        {
            // Don't compare against one of the two source-pieces
            if (turn.operations[i].piece_index == piece.index)
            {
                return;
            }

            Piece const& pc = m_pieces[turn.operations[i].piece_index];

            if (pc.left_index == piece.index
                || pc.right_index == piece.index)
            {
                if (pc.type == strategy::buffer::buffered_flat_end)
                {
                    // If it is a flat end, don't compare against its neighbor:
                    // it will always be located on one of the helper segments
                    return;
                }
                if (pc.type == strategy::buffer::buffered_concave)
                {
                    // If it is concave, the same applies: the IP will be
                    // located on one of the helper segments
                    return;
                }
                neighbour = true;
            }
        }

        // TODO: mutable_piece to make some on-demand preparations in analyse
        analyse_result analyse_code
            = analyse_turn_wrt_piece::apply(turn.robust_point, piece);

        Turn& mutable_turn = m_turns[turn.turn_index];
        switch(analyse_code)
        {
            case analyse_disjoint :
                return;
            case analyse_on_offsetted :
                mutable_turn.count_on_offsetted++; // value is not used anymore
                return;
            case analyse_on_original_boundary :
                mutable_turn.count_on_original_boundary++;
                return;
            case analyse_within :
                mutable_turn.count_within++;
                return;
            default :
                break;
        }

        // TODO: this point_in_geometry is a performance-bottleneck here and
        // will be replaced completely by extending analyse_piece functionality
        int geometry_code = detail::within::point_in_geometry(turn.robust_point, piece.robust_ring);

        if (geometry_code == -1)
        {
            // Outside, always return
            return;
        }

        if (geometry_code == 0 && neighbour)
        {
            // The IP falling on the border of its neighbour is a normal situation
            return;
        }

        switch(analyse_code)
        {
            case analyse_on_offsetted :
                mutable_turn.count_on_offsetted++; // value is not used anymore
                break;
            case analyse_near_offsetted :
                if (geometry_code == 1)
                {
                    mutable_turn.count_within_near_offsetted++;
                }
                break;
            default :
                mutable_turn.count_within++;
                break;
        }
    }
};


}} // namespace detail::buffer
#endif // DOXYGEN_NO_DETAIL


}} // namespace boost::geometry

#endif // BOOST_GEOMETRY_ALGORITHMS_DETAIL_BUFFER_TURN_IN_PIECE_VISITOR
