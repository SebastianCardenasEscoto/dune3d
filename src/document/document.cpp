#include "document.hpp"
#include "nlohmann/json.hpp"
#include "entity/entity.hpp"
#include "constraint/constraint.hpp"
#include "group/group.hpp"
#include "util/util.hpp"
#include "group/group_extrude.hpp"
#include "group/group_reference.hpp"
#include "group/group_sketch.hpp"
#include "system/system.hpp"
#include <ranges>
#include <set>
#include <algorithm>
#include <iostream>
#include "entity/entity_and_point.hpp"

namespace dune3d {

static void to_json(nlohmann::json &j, const std::unique_ptr<Entity> &e)
{
    j = e->serialize();
}

static void to_json(nlohmann::json &j, const std::unique_ptr<Constraint> &e)
{
    j = e->serialize();
}

json Document::serialize() const
{
    auto j = json{{"type", "document"}};
    m_version.serialize(j);
    {
        auto o = json::object();
        for (const auto &[uu, it] : m_groups) {
            o[uu] = it->serialize(*this);
        }
        j["groups"] = o;
    }
    {
        auto o = json::object();
        for (const auto &[uu, it] : m_entities) {
            if (it->m_kind == ItemKind::USER)
                o[uu] = it->serialize();
        }
        j["entities"] = o;
    }
    {
        auto o = json::object();
        for (const auto &[uu, it] : m_constraints) {
            o[uu] = it->serialize();
        }
        j["constraints"] = o;
    }
    return j;
}

static const unsigned int app_version = 0;

unsigned int Document::get_app_version()
{
    return app_version;
}

Document::Document() : m_version(app_version)
{
    auto &grp = add_group<GroupReference>(UUID::random());
    grp.m_name = "Reference";
    grp.m_body.emplace();

    auto &sketch = add_group<GroupSketch>(UUID::random());
    sketch.set_index({}, 1);
    sketch.m_name = "Sketch";
    sketch.m_active_wrkpl = grp.get_workplane_xy_uuid();

    set_group_generate_pending(grp.m_uuid);
    update_pending();
}

Document::Document(const json &j, const std::filesystem::path &containing_dir) : m_version(app_version, j)
{
    for (const auto &[uu, it] : j.at("entities").items()) {
        m_entities.emplace(std::piecewise_construct, std::forward_as_tuple(uu),
                           std::forward_as_tuple(Entity::new_from_json(uu, it, containing_dir)));
    }
    for (const auto &[uu, it] : j.at("constraints").items()) {
        m_constraints.emplace(std::piecewise_construct, std::forward_as_tuple(uu),
                              std::forward_as_tuple(Constraint::new_from_json(uu, it)));
    }
    for (const auto &[uu, it] : j.at("groups").items()) {
        m_groups.emplace(std::piecewise_construct, std::forward_as_tuple(uu),
                         std::forward_as_tuple(Group::new_from_json(uu, it)));
    }

    if (m_groups.size())
        set_group_generate_pending(get_groups_sorted().front()->m_uuid);
    update_pending();

    erase_invalid();
}

void Document::erase_invalid()
{
    map_erase_if(m_constraints, [this](auto &x) { return !x.second->is_valid(*this); });
}

Document::Document(const Document &other) : m_version(other.m_version)
{
    for (const auto &[uu, it] : other.m_entities) {
        m_entities.emplace(uu, it->clone());
    }
    for (const auto &[uu, it] : other.m_constraints) {
        m_constraints.emplace(uu, it->clone());
    }
    for (const auto &[uu, it] : other.m_groups) {
        m_groups.emplace(uu, it->clone());
    }
}

Document Document::new_from_file(const std::filesystem::path &path)
{
    return Document{load_json_from_file(path), path.parent_path()};
}

std::vector<Group *> Document::get_groups_sorted()
{
    auto groups = static_cast<const Document *>(this)->get_groups_sorted();
    std::vector<Group *> r;
    r.reserve(groups.size());
    for (auto g : groups) {
        r.push_back(const_cast<Group *>(g));
    }
    return r;
}
std::vector<const Group *> Document::get_groups_sorted() const
{
    std::vector<const Group *> r;
    r.reserve(m_groups.size());
    for (auto &[uu, it] : m_groups) {
        r.push_back(it.get());
    }
    std::ranges::sort(r, {}, [](auto a) { return a->get_index(); });
    return r;
}

std::vector<Document::BodyGroups> Document::get_groups_by_body() const
{
    std::vector<Document::BodyGroups> r;
    for (auto group : get_groups_sorted()) {
        if (group->m_body) {
            r.emplace_back(group->m_body.value());
        }
        assert(r.size());
        r.back().groups.push_back(group);
    }

    return r;
}

void Document::update_pending(const UUID &last_group_to_update, const std::vector<EntityAndPoint> &dragged)
{
    auto groups_sorted = get_groups_sorted();
    auto get_first_index = [this](const UUID &uu) {
        if (m_groups.contains(uu))
            return get_group(uu).get_index();
        else
            return INT_MAX;
    };

    const auto first_generate_index = get_first_index(m_first_group_generate);
    const auto first_solve_index = get_first_index(m_first_group_solve);
    const auto first_update_solid_model_index = get_first_index(m_first_group_update_solid_model);
    const Group *last_group = nullptr;
    // first pass: generate
    if (m_first_group_generate) {
        for (auto group : groups_sorted) {
            if (last_group && last_group->m_uuid == last_group_to_update) {
                // we've seen all groups we needed to see, update to the rest
                if (m_first_group_generate)
                    m_first_group_generate = group->m_uuid;
                break;
            }
            const auto index = group->get_index();
            if (index >= first_generate_index) {
                generate_group(*group);
            }
            last_group = group;
        }
    }
    if (!last_group_to_update)
        m_first_group_generate = UUID();

    erase_invalid();

    last_group = nullptr;
    for (auto group : groups_sorted) {
        if (last_group && last_group->m_uuid == last_group_to_update) {
            // we've seen all groups we needed to see, update to the rest
            if (m_first_group_solve)
                m_first_group_solve = group->m_uuid;
            if (m_first_group_update_solid_model)
                m_first_group_update_solid_model = group->m_uuid;
            return;
        }
        const auto index = group->get_index();
        if (index >= first_solve_index) {
            solve_group(*group, dragged);
        }
        if (index >= first_update_solid_model_index) {
            update_solid_model(*group);
        }

        last_group = group;
    }
    // we've seen all groups, reset all pendings
    m_first_group_solve = UUID();
    m_first_group_update_solid_model = UUID();
}

void Document::generate_group(Group &group)
{
    if (auto gg = dynamic_cast<IGroupGenerate *>(&group)) {
        for (auto &[uu, it] : m_entities) {
            if (it->m_group == group.m_uuid && it->m_kind == ItemKind::GENRERATED)
                it->m_kind = ItemKind::GENRERATED_STALE;
        }
        gg->generate(*this);
        map_erase_if(m_entities, [&group](auto &x) {
            return x.second->m_group == group.m_uuid && x.second->m_kind == ItemKind::GENRERATED_STALE;
        });
    }
}


void Document::update_solid_model(Group &group)
{
    if (auto gr = dynamic_cast<IGroupSolidModel *>(&group))
        gr->update_solid_model(*this);
}

void Document::solve_group(Group &group, const std::vector<EntityAndPoint> &dragged)
{
    if (group.get_type() == Group::Type::REFERENCE) {
        group.m_dof = 0;
        return;
    }
    System system{*this, group.m_uuid};
    for (const auto &[en, pt] : dragged) {
        system.add_dragged(en, pt);
    }
    system.solve();
    system.update_document();
}

void Document::insert_group(std::unique_ptr<Group> new_group, const UUID &after)
{
    auto &gr_after = *m_groups.at(after);
    new_group->set_index({}, gr_after.get_index() + 1);
    for (auto &[uu, group] : m_groups) {
        if (group->get_index() > gr_after.get_index())
            group->set_index({}, group->get_index() + 1);
    }
    m_groups.emplace(new_group->m_uuid, std::move(new_group));
}

UUID Document::get_group_rel(const UUID &group, int delta) const
{
    auto groups = get_groups_sorted();
    auto &current_group = get_group(group);
    int pos = std::ranges::find(groups, &current_group) - groups.begin();
    pos += delta;
    if (pos < 0 || pos >= (int)groups.size())
        return UUID();
    auto next_group = groups.at(pos);
    return next_group->m_uuid;
}

bool Document::reorder_group(const UUID &group_uu, const UUID &after)
{
    auto groups_sorted = get_groups_sorted();
    decltype(groups_sorted) groups_new_order;
    groups_new_order.reserve(groups_sorted.size());

    for (auto gr : groups_sorted) {
        if (gr->m_uuid == group_uu)
            continue;
        groups_new_order.push_back(gr);
        if (gr->m_uuid == after)
            groups_new_order.push_back(&get_group(group_uu));
    }


    std::set<UUID> available_groups;
    for (auto gr : groups_new_order) {
        available_groups.insert(gr->m_uuid);
        auto referenced_groups = gr->get_referenced_groups(*this);
        if (!std::ranges::includes(available_groups, referenced_groups))
            return false;
    }

    {
        unsigned int index = 0;
        for (auto gr : groups_new_order) {
            gr->set_index({}, index++);
        }
    }

    return true;
}

void Document::ItemsToDelete::append(const ItemsToDelete &other)
{
    entities.insert(other.entities.begin(), other.entities.end());
    groups.insert(other.groups.begin(), other.groups.end());
    constraints.insert(other.constraints.begin(), other.constraints.end());
}

static void subtract_set(std::set<UUID> &s, const std::set<UUID> &sub)
{
    for (const auto &it : sub) {
        s.erase(it);
    }
}

void Document::ItemsToDelete::subtract(const ItemsToDelete &other)
{
    subtract_set(entities, other.entities);
    subtract_set(groups, other.groups);
    subtract_set(constraints, other.constraints);
}

bool Document::ItemsToDelete::empty() const
{
    return entities.empty() && groups.empty() && constraints.empty();
}

size_t Document::ItemsToDelete::size() const
{
    return entities.size() + groups.size() + constraints.size();
}

void Document::accumulate_first_group(const Group *&first_group, const UUID &group_uu) const
{
    auto &group = get_group(group_uu);
    if (!first_group || group.get_index() < first_group->get_index())
        first_group = &group;
}

UUID Document::ItemsToDelete::get_first_group(const Document &doc) const
{
    const Group *first_group = nullptr;
    for (const auto &uu : groups) {
        doc.accumulate_first_group(first_group, uu);
    }
    for (const auto &uu : entities) {
        auto &en = doc.get_entity(uu);
        doc.accumulate_first_group(first_group, en.m_group);
    }
    for (const auto &uu : constraints) {
        auto &co = doc.get_constraint(uu);
        doc.accumulate_first_group(first_group, co.m_group);
    }

    if (first_group)
        return first_group->m_uuid;
    else
        return UUID();
}

Document::ItemsToDelete Document::get_additional_items_to_delete(const ItemsToDelete &items_initial) const
{
    ItemsToDelete items = items_initial;

    while (1) {
        auto size_before = items.size();
        for (const auto &[uu, it] : m_entities) {
            if (items.entities.contains(uu))
                continue;
            if (items.groups.contains(it->m_group)) {
                items.entities.insert(uu);
                continue;
            }
            auto refs = it->get_referenced_entities();
            for (auto &ref : refs) {
                if (items.entities.contains(ref)) {
                    items.entities.insert(uu);
                    break;
                }
            }
        }

        for (const auto &[uu, it] : m_constraints) {
            if (items.constraints.contains(uu))
                continue;
            if (items.groups.contains(it->m_group)) {
                items.entities.insert(uu);
                continue;
            }
            auto refs = it->get_referenced_entities();
            for (auto &ref : refs) {
                if (items.entities.contains(ref)) {
                    items.constraints.insert(uu);
                    break;
                }
            }
        }

        for (const auto &[uu, it] : m_groups) {
            if (items.groups.contains(uu))
                continue;
            {
                auto reqs = it->get_required_entities(*this);
                for (auto &req : reqs) {
                    if (items.entities.contains(req)) {
                        items.groups.insert(uu);
                        break;
                    }
                }
            }
            {
                auto reqs = it->get_required_groups(*this);
                for (auto &req : reqs) {
                    if (items.groups.contains(req)) {
                        items.groups.insert(uu);
                        break;
                    }
                }
            }
        }

        if (size_before == items.size())
            break;
    }

    items.subtract(items_initial);

    return items;
}

void Document::delete_items(const ItemsToDelete &items)
{
    set_group_generate_pending(items.get_first_group(*this));
    for (auto &it : items.entities) {
        m_entities.erase(it);
    }
    for (auto &it : items.groups) {
        m_groups.erase(it);
    }
    for (auto &it : items.constraints) {
        m_constraints.erase(it);
    }

    for (auto &[uu, gr] : m_groups) {
        if (!gr->m_active_wrkpl)
            continue;
        if (!m_entities.contains(gr->m_active_wrkpl))
            gr->m_active_wrkpl = UUID();
    }
}

glm::dvec3 Document::get_point(const EntityAndPoint &ep) const
{
    return get_entity(ep.entity).get_point(ep.point, *this);
}

bool Document::is_valid_point(const EntityAndPoint &ep) const
{
    return get_entity(ep.entity).is_valid_point(ep.point);
}

void Document::update_group_if_less(UUID &uu, const UUID &new_group)
{
    if (uu == UUID()) {
        uu = new_group;
        return;
    }
    auto current_index = get_group(uu).get_index();
    auto new_index = get_group(new_group).get_index();
    if (new_index < current_index)
        uu = new_group;
}

void Document::set_group_generate_pending(const UUID &group)
{
    update_group_if_less(m_first_group_generate, group);
    set_group_solve_pending(group);
}

void Document::set_group_solve_pending(const UUID &group)
{
    update_group_if_less(m_first_group_solve, group);
    set_group_update_solid_model_pending(group);
}

void Document::set_group_update_solid_model_pending(const UUID &group)
{
    update_group_if_less(m_first_group_update_solid_model, group);
}

Document::~Document() = default;

} // namespace dune3d
