#include <cmath>

#include "plugin-editor.h"
#include "plugin-processor.h"

// Grouped control surface for the plugin (see plugin-editor.h). Everything here
// runs on the message thread; parameter attachments keep the knobs and the host
// in sync. The look is deliberate: near-black warm charcoal, a single amber /
// tape accent, arc-drawn knobs, section eyebrows with hairline rules.

namespace acfx::plugin {

namespace {

// --- palette (warm dark, single amber accent — not the generic acid green) ---
const juce::Colour kBg          (0xff17181a);
const juce::Colour kPanel       (0xff202327);
const juce::Colour kText        (0xffe9e4da);
const juce::Colour kMuted       (0xff8b867d);
const juce::Colour kAccent      (0xffe0a34a); // amber
const juce::Colour kTrack       (0xff34383d);

// Layout metrics.
constexpr int kPad       = 16;
constexpr int kHeaderH   = 52;
constexpr int kSecHeadH  = 24;
constexpr int kCellW     = 86;
constexpr int kCellH     = 88;
constexpr int kSecGap    = 8;
constexpr int kColCells  = 6;                 // cells per row within one column (a 6-line array = one clean row)
constexpr int kColW      = kColCells * kCellW; // one column's content width
constexpr int kColGap    = 20;                // gutter between the two columns
// Above this single-column content height, split sections into two columns so
// the window still fits a laptop screen.
constexpr int kMaxColH   = 760;

class AcfxLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    AcfxLookAndFeel() {
        setColour(juce::Slider::textBoxTextColourId, kText);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::textColourId, kMuted);
        setColour(juce::ComboBox::backgroundColourId, kPanel);
        setColour(juce::ComboBox::textColourId, kText);
        setColour(juce::ComboBox::arrowColourId, kAccent);
        setColour(juce::ComboBox::outlineColourId, kTrack);
        setColour(juce::PopupMenu::backgroundColourId, kPanel);
        setColour(juce::PopupMenu::textColourId, kText);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.28f));
        setColour(juce::PopupMenu::highlightedTextColourId, kText);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override {
        const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(7.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float angle = startAngle + pos * (endAngle - startAngle);

        // knob body
        g.setColour(kPanel);
        g.fillEllipse(cx - radius * 0.72f, cy - radius * 0.72f, radius * 1.44f, radius * 1.44f);

        // track arc
        juce::Path track;
        track.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour(kTrack);
        g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        // value arc
        juce::Path val;
        val.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour(kAccent);
        g.strokePath(val, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        // pointer dot (JUCE angle is clockwise from 12 o'clock)
        const float tipX = cx + radius * 0.62f * std::sin(angle);
        const float tipY = cy - radius * 0.62f * std::cos(angle);
        g.setColour(kAccent);
        g.fillEllipse(tipX - 2.4f, tipY - 2.4f, 4.8f, 4.8f);
    }
};

} // namespace

PluginEditor::PluginEditor(PluginProcessor& processor)
    : juce::AudioProcessorEditor(processor) {
    lnf_ = std::make_unique<AcfxLookAndFeel>();
    setLookAndFeel(lnf_.get());

    // Header eyebrow shows the effect's own name (was hardcoded), so every
    // plugin built from this shared editor is labelled correctly.
    title_ = processor.getName().trimCharactersAtStart("acfx ").toUpperCase();

    buildControls(processor);
    planLayout();
    setSize(contentW_, contentH_);
}

PluginEditor::~PluginEditor() { setLookAndFeel(nullptr); }

void PluginEditor::buildControls(PluginProcessor& processor) {
    for (const juce::AudioProcessorParameterGroup* group :
         processor.getParameterTree().getSubgroups(false)) {
        Section section;
        section.name = group->getName();

        for (juce::AudioProcessorParameter* p : group->getParameters(false)) {
            const juce::String label = p->getName(64);

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p)) {
                auto cell = std::make_unique<Choice>();
                cell->box.addItemList(choice->choices, 1);
                cell->box.setJustificationType(juce::Justification::centred);
                cell->attach = std::make_unique<juce::ComboBoxParameterAttachment>(*choice, cell->box);
                cell->label.setText(label, juce::dontSendNotification);
                cell->label.setJustificationType(juce::Justification::centred);
                cell->label.setFont(juce::Font(11.0f));
                addAndMakeVisible(cell->box);
                addAndMakeVisible(cell->label);
                section.cells.push_back({true, static_cast<int>(choices_.size())});
                choices_.push_back(std::move(cell));
            } else if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p)) {
                auto cell = std::make_unique<Knob>();
                cell->slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
                cell->slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                                 juce::MathConstants<float>::pi * 2.75f, true);
                cell->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kCellW - 8, 16);
                cell->slider.setColour(juce::Slider::textBoxTextColourId, kText);
                cell->attach = std::make_unique<juce::SliderParameterAttachment>(*ranged, cell->slider);
                cell->label.setText(label, juce::dontSendNotification);
                cell->label.setJustificationType(juce::Justification::centred);
                cell->label.setFont(juce::Font(11.0f));
                addAndMakeVisible(cell->slider);
                addAndMakeVisible(cell->label);
                section.cells.push_back({false, static_cast<int>(knobs_.size())});
                knobs_.push_back(std::move(cell));
            }
        }
        sections_.push_back(std::move(section));
    }
}

void PluginEditor::planLayout() {
    const int n = static_cast<int>(sections_.size());
    auto secHeight = [&](int i) {
        return kSecHeadH + sections_[static_cast<std::size_t>(i)].rows * kCellH + kSecGap;
    };
    int total = 0;
    for (int i = 0; i < n; ++i) {
        Section& s = sections_[static_cast<std::size_t>(i)];
        s.rows = juce::jmax(1, (static_cast<int>(s.cells.size()) + kColCells - 1) / kColCells);
        total += secHeight(i);
    }

    numColumns_ = (total > kMaxColH && n > 1) ? 2 : 1;
    int colHeight = total;
    if (numColumns_ == 2) {
        // Best contiguous split: first k sections in column 0, rest in column 1,
        // minimising the taller column (balances by height, not section count).
        int best = total, bestK = 1, left = 0;
        for (int k = 0; k < n; ++k) {
            left += secHeight(k);
            const int taller = juce::jmax(left, total - left);
            if (k < n - 1 && taller < best) { best = taller; bestK = k + 1; }
        }
        int c0 = 0, c1 = 0;
        for (int i = 0; i < n; ++i) {
            const bool right = i >= bestK;
            sections_[static_cast<std::size_t>(i)].column = right ? 1 : 0;
            (right ? c1 : c0) += secHeight(i);
        }
        colHeight = juce::jmax(c0, c1);
    } else {
        for (Section& s : sections_) s.column = 0;
    }

    contentW_ = 2 * kPad + numColumns_ * kColW + (numColumns_ - 1) * kColGap;
    contentH_ = kHeaderH + colHeight + kPad;
}

void PluginEditor::resized() {
    int colY[2] = {kHeaderH, kHeaderH};
    const int colX[2] = {kPad, kPad + kColW + kColGap};

    for (Section& s : sections_) {
        const int col = s.column;
        auto area = juce::Rectangle<int>(colX[col], colY[col], kColW,
                                         kSecHeadH + s.rows * kCellH);
        s.headerArea = area.removeFromTop(kSecHeadH);
        int i = 0;
        while (i < static_cast<int>(s.cells.size())) {
            auto rowArea = area.removeFromTop(kCellH);
            for (int c = 0; c < kColCells && i < static_cast<int>(s.cells.size()); ++c, ++i) {
                auto cellBounds = rowArea.removeFromLeft(kCellW);
                const Cell& cell = s.cells[static_cast<std::size_t>(i)];
                auto labelArea = cellBounds.removeFromTop(16);
                if (cell.isChoice) {
                    Choice& ch = *choices_[static_cast<std::size_t>(cell.index)];
                    ch.label.setBounds(labelArea);
                    ch.box.setBounds(cellBounds.reduced(6, 20));
                } else {
                    Knob& kb = *knobs_[static_cast<std::size_t>(cell.index)];
                    kb.label.setBounds(labelArea);
                    kb.slider.setBounds(cellBounds);
                }
            }
        }
        colY[col] += kSecHeadH + s.rows * kCellH + kSecGap;
    }
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(kBg);

    // Header: title + a small reverse-arc motif as the signature nod to "reverse".
    auto header = getLocalBounds().removeFromTop(kHeaderH);
    auto titleArea = header.reduced(kPad, 0);

    const float cy = titleArea.getCentreY();
    const float mx = static_cast<float>(titleArea.getRight()) - 12.0f;
    juce::Path arc;
    arc.addCentredArc(mx, cy, 9.0f, 9.0f, 0.0f,
                      juce::MathConstants<float>::pi * 0.15f,
                      juce::MathConstants<float>::pi * 1.7f, true);
    g.setColour(kAccent);
    g.strokePath(arc, juce::PathStrokeType(2.0f));
    g.fillEllipse(mx - 10.0f, cy - 9.0f - 2.0f, 4.0f, 4.0f); // arrowhead dot

    g.setColour(kText);
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.drawText("acfx", titleArea.removeFromLeft(56), juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(15.0f));
    g.drawText(title_, titleArea, juce::Justification::centredLeft);

    // Build stamp so you can confirm which version is loaded.
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    g.drawText(juce::String("build ") + __DATE__ + " " + __TIME__,
               getLocalBounds().removeFromTop(kHeaderH).reduced(kPad, 6),
               juce::Justification::bottomRight);

    g.setColour(kTrack);
    g.fillRect(getLocalBounds().removeFromTop(kHeaderH).removeFromBottom(1));

    // Section eyebrows + hairline rules.
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    for (const Section& s : sections_) {
        auto hb = s.headerArea;
        g.setColour(kAccent);
        g.drawText(s.name.toUpperCase(), hb.removeFromLeft(140),
                   juce::Justification::centredLeft);
        // hairline rule filling the space to the right of the eyebrow
        auto ruleRow = hb.withTrimmedLeft(8);
        g.setColour(kTrack);
        g.fillRect(ruleRow.getX(), ruleRow.getCentreY(), ruleRow.getWidth(), 1);
    }
}

} // namespace acfx::plugin
