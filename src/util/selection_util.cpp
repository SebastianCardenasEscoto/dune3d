#include "selection_util.hpp"
#include "document/document.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_line3d.hpp"
#include "document/entity/entity_line3d.hpp"
#include "canvas/selectable_ref.hpp"
#include <algorithm>

namespace dune3d {

std::set<EntityAndPoint> TwoPoints::get_enps() const
{
    return {point1, point2};
}

std::set<EntityAndPoint> LineAndPoint::get_enps() const
{
    return {{line, 0}, point};
}

std::optional<TwoPoints> two_points_from_selection(const Document &doc, const std::set<SelectableRef> &sel)
{
    if (sel.size() == 1) {
        auto &sr = *sel.begin();
        if (sr.type != SelectableRef::Type::ENTITY)
            return {};
        auto &en = doc.get_entity(sr.item);
        if (en.get_type() == Entity::Type::LINE_2D && sr.point == 0) {
            auto &en2 = dynamic_cast<const EntityLine2D &>(en);
            return TwoPoints{{en2.m_uuid, 1}, {en2.m_uuid, 2}};
        }
        else if (en.get_type() == Entity::Type::LINE_3D && sr.point == 0) {
            auto &en3 = dynamic_cast<const EntityLine3D &>(en);
            return TwoPoints{{en3.m_uuid, 1}, {en3.m_uuid, 2}};
        }
        else {
            return {};
        }
    }
    else if (sel.size() == 2) {
        auto it = sel.begin();
        auto &sr1 = *it++;
        auto &sr2 = *it;

        if (sr1.type != SelectableRef::Type::ENTITY)
            return {};
        if (sr2.type != SelectableRef::Type::ENTITY)
            return {};

        auto &en1 = doc.get_entity(sr1.item);
        auto &en2 = doc.get_entity(sr2.item);
        if (!en1.is_valid_point(sr1.point))
            return {};
        if (!en2.is_valid_point(sr2.point))
            return {};

        return TwoPoints{sr1.get_entity_and_point(), sr2.get_entity_and_point()};
    }
    else {
        return {};
    }
}


std::optional<EntityAndPoint> entity_and_point_from_hover_selection(const Document &doc,
                                                                    const std::optional<SelectableRef> &hover_sel)
{
    if (!hover_sel.has_value())
        return {};
    if (hover_sel->type != SelectableRef::Type::ENTITY)
        return {};
    auto &entity = doc.get_entity(hover_sel->item);
    if (entity.is_valid_point(hover_sel->point))
        return {{hover_sel->item, hover_sel->point}};
    return {};
}


std::optional<LineAndPoint> line_and_point_from_selection(const Document &doc, const std::set<SelectableRef> &sel,
                                                          LineAndPoint::AllowSameEntity allow_same_entity)
{
    if (sel.size() != 2)
        return {};
    auto it = sel.begin();
    auto sr1 = *it++;
    auto sr2 = *it;

    if (sr1.type != SelectableRef::Type::ENTITY)
        return {};
    if (sr2.type != SelectableRef::Type::ENTITY)
        return {};

    if ((allow_same_entity == LineAndPoint::AllowSameEntity::NO) && (sr1.item == sr2.item))
        return {};

    // for an sr to be a line, it must be of LINE2D/3D and point==0
    if (doc.is_valid_point(sr2.get_entity_and_point()))
        std::swap(sr1, sr2);

    auto &sr_point = sr1;
    auto &sr_line = sr2;

    if (!doc.is_valid_point(sr_point.get_entity_and_point()))
        return {};

    if (sr_line.point != 0)
        return {};

    auto &en_line = doc.get_entity(sr_line.item);
    if (!en_line.of_type(Entity::Type::LINE_2D, Entity::Type::LINE_3D))
        return {};

    return {{sr_line.item, sr_point.get_entity_and_point()}};
}

std::optional<LineAndPoint> circle_and_point_from_selection(const Document &doc, const std::set<SelectableRef> &sel,
                                                            LineAndPoint::AllowSameEntity allow_same_entity)
{
    if (sel.size() != 2)
        return {};
    auto it = sel.begin();
    auto &sr1 = *it++;
    auto &sr2 = *it;

    if (sr1.type != SelectableRef::Type::ENTITY)
        return {};
    if (sr2.type != SelectableRef::Type::ENTITY)
        return {};

    if ((allow_same_entity == LineAndPoint::AllowSameEntity::NO) && (sr1.item == sr2.item))
        return {};

    if ((!!sr1.point) == (!!sr2.point))
        return {};

    auto &sr_line = sr1.point == 0 ? sr1 : sr2;
    auto &sr_point = sr1.point == 0 ? sr2 : sr1;

    auto &en_line = doc.get_entity(sr_line.item);
    if (en_line.get_type() != Entity::Type::ARC_2D && en_line.get_type() != Entity::Type::ARC_3D
        && en_line.get_type() != Entity::Type::CIRCLE_2D && en_line.get_type() != Entity::Type::CIRCLE_3D)
        return {};

    return {{sr_line.item, sr_point.get_entity_and_point()}};
}

std::optional<EntityAndPoint> entity_and_point_from_selection(const Document &doc, const std::set<SelectableRef> &sel)
{
    if (sel.size() != 1)
        return {};
    auto it = sel.begin();
    auto &sr1 = *it;

    if (sr1.type != SelectableRef::Type::ENTITY)
        return {};

    if (!doc.m_entities.contains(sr1.item))
        return {};

    return sr1.get_entity_and_point();
}

std::optional<EntityAndPoint> entity_and_point_from_selection(const Document &doc, const std::set<SelectableRef> &sel,
                                                              Entity::Type type)
{
    auto enp = entity_and_point_from_selection(doc, sel);
    if (!enp)
        return {};

    if (!doc.get_entity(enp->entity).of_type(type))
        return {};

    return enp;
}

std::optional<LinesAndPoint> lines_and_point_from_selection(const Document &doc, const std::set<SelectableRef> &sel)
{
    if (sel.size() != 3)
        return {};
    auto it = sel.begin();
    auto &sr1 = *it++;
    auto &sr2 = *it++;
    auto &sr3 = *it;

    if (sr1.type != SelectableRef::Type::ENTITY)
        return {};
    if (sr2.type != SelectableRef::Type::ENTITY)
        return {};
    if (sr3.type != SelectableRef::Type::ENTITY)
        return {};

    std::array<const SelectableRef *, 3> srs = {&sr1, &sr2, &sr3};

    for (size_t i = 0; i < 6; i++) {
        std::ranges::next_permutation(srs);

        auto enp_pt = srs.at(0)->get_entity_and_point();

        LinesAndPoint r;
        if (doc.is_valid_point(enp_pt)) {
            r.point = enp_pt;
            if (srs.at(1)->point != 0)
                return {};
            if (srs.at(2)->point != 0)
                return {};
            if (!doc.get_entity(srs.at(1)->item).of_type(Entity::Type::LINE_2D, Entity::Type::LINE_3D))
                return {};
            if (!doc.get_entity(srs.at(2)->item).of_type(Entity::Type::LINE_2D, Entity::Type::LINE_3D))
                return {};
            r.lines.at(0) = srs.at(1)->item;
            r.lines.at(1) = srs.at(2)->item;
            return r;
        }
    }


    return {};
}


} // namespace dune3d
