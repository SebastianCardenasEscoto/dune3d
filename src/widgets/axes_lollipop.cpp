#include "axes_lollipop.hpp"
#include "color.hpp"
#include <glm/gtx/rotate_vector.hpp>

namespace dune3d {

static const std::array<std::string, 3> s_xyz = {"X", "Y", "Z"};

static const Color &get_color(unsigned int ax, float z)
{
    static const std::array<Color, 3> ax_colors_pos = {
            Color::new_from_int(255, 54, 83),
            Color::new_from_int(138, 219, 0),
            Color::new_from_int(44, 142, 254),
    };
    static const std::array<Color, 3> ax_colors_neg = {
            Color::new_from_int(155, 57, 7),
            Color::new_from_int(98, 137, 34),
            Color::new_from_int(51, 100, 155),
    };
    if (z >= -.001)
        return ax_colors_pos.at(ax);
    else
        return ax_colors_neg.at(ax);
}

AxesLollipop::AxesLollipop()
{
    create_layout();
    for (unsigned int ax = 0; ax < 3; ax++) {
        m_layout->set_text(s_xyz.at(ax));
        auto ext = m_layout->get_pixel_logical_extents();
        m_size = std::max(m_size, (float)ext.get_width());
        m_size = std::max(m_size, (float)ext.get_height());
    }
    set_content_height(100);
    set_content_width(120);
    set_draw_func(sigc::mem_fun(*this, &AxesLollipop::render));
    // signal_screen_changed().connect([this](const Glib::RefPtr<Gdk::Screen> &screen) { create_layout(); });
}

void AxesLollipop::create_layout()
{
    m_layout = create_pango_layout("");
    Pango::AttrList attrs;
    auto attr = Pango::Attribute::create_attr_weight(Pango::Weight::BOLD);
    attrs.insert(attr);
    m_layout->set_attributes(attrs);
}

void AxesLollipop::set_quat(const glm::quat &q)
{
    m_quat = q;
    queue_draw();
}

void AxesLollipop::drawFace(const Cairo::RefPtr<Cairo::Context> &cr, double x1,double x2,double y1,double y2){
    cr->move_to(x1, y1);
    cr->line_to(x2, y1);
    cr->line_to(x2, y2);
    cr->line_to(x1, y2);
    cr->move_to(x1, y1);
    cr->stroke();
}

void AxesLollipop::render(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
{
    const float sc = (std::min(w, h) / 2) - m_size;
    cr->translate(w / 2, h / 2);
    cr->set_line_width(2);
    double size = 1.0; // Size of the cube
    const double vertices[8][3] = {
    {-size, -size, -size},  // 0
    { size, -size, -size},  // 1
    { size,  size, -size},  // 2
    {-size,  size, -size},  // 3
    {-size, -size,  size},  // 4
    { size, -size,  size},  // 5
    { size,  size,  size},  // 6
    {-size,  size,  size}   // 7
    };
    const int edges[12][2] = {
    {0, 1},  // Edge 0: Vertices 0-1
    {1, 2},  // Edge 1: Vertices 1-2
    {2, 3},  // Edge 2: Vertices 2-3
    {3, 0},  // Edge 3: Vertices 3-0

    {4, 5},  // Edge 4: Vertices 4-5
    {5, 6},  // Edge 5: Vertices 5-6
    {6, 7},  // Edge 6: Vertices 6-7
    {7, 4},  // Edge 7: Vertices 7-4

    {0, 4},  // Edge 8: Vertices 0-4
    {1, 5},  // Edge 9: Vertices 1-5
    {2, 6},  // Edge 10: Vertices 2-6
    {3, 7}   // Edge 11: Vertices 3-7
    };
    std::vector<std::pair<glm::vec3, glm::vec3>> pts;
    for (unsigned int i = 0; i < 12; i++) {
        const glm::vec3 v1(vertices[edges[i][0]][0], vertices[edges[i][0]][1], vertices[edges[i][0]][2]);
        const glm::vec3 v2(vertices[edges[i][1]][0], vertices[edges[i][1]][1], vertices[edges[i][1]][2]);
        const auto vt1 = glm::rotate(glm::inverse(m_quat), v1) * sc;
        const auto vt2 = glm::rotate(glm::inverse(m_quat), v2) * sc;
        pts.emplace_back(vt1, vt2);
    }

    for (const auto &[u, v] : pts) {
        cr->move_to(u.x, u.y);
        cr->line_to(v.x, v.y);
        cr->set_source_rgb(0, 0, 0);
        cr->stroke();
    }
}

} // namespace dune3d
