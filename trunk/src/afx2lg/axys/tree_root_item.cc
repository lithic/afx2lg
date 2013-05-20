// Copyright (c) 2013, Tomas Gunnarsson
// All rights reserved.

#include "axys/tree_root_item.h"

#include "axefx/axe_fx_sysex_parser.h"
#include "axefx/preset.h"
#include "axys/tree_preset_item.h"

using axefx::SysExParser;
using namespace juce;

namespace {
class ComparePresets {
 public:
  int compareElements(TreeViewItem* first, TreeViewItem* second) {
    auto p1 = static_cast<const PresetItem*>(first);
    auto p2 = static_cast<const PresetItem*>(second);
    if (p1->id() < p2->id())
      return -1;

    if (p1->id() > p2->id())
      return 1;

    return 0;
  }
};

void refreshTree(TreeRootItem* root, bool sort_first) {
  if (sort_first)
    root->sortPresets();
  root->treeHasChanged();
}

bool isDeleteKey(const KeyPress& key) {
  const KeyPress del_key(KeyPress::deleteKey);
#ifdef OS_MACOSX
  const KeyPress backspace_key(KeyPress::backspaceKey);
  return key == del_key ||
         key == backspace_key;
#else
  return key == del_key;
#endif
}

bool isMoveUpKey(const KeyPress& key) {
  const KeyPress move_up_key(KeyPress::upKey, ModifierKeys::commandModifier, 0);
  return key == move_up_key;
}

bool isMoveDownKey(const KeyPress& key) {
  const KeyPress move_down_key(KeyPress::downKey,
      ModifierKeys::commandModifier, 0);
  return key == move_down_key;
}

bool isUndoKey(const KeyPress& key) {
  const ModifierKeys mods(ModifierKeys::commandModifier);
  const KeyPress undo_key('z', mods, 0);
  return key == undo_key;
}

bool isRedoKey(const KeyPress& key) {
  const KeyPress redo_key1('y', ModifierKeys::commandModifier, 0);
  const KeyPress redo_key2('z',
      ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0);
  return key == redo_key1 || key == redo_key2;
}

class UndoablePresetAction : public juce::UndoableAction {
 public:
  UndoablePresetAction(TreeRootItem* root, bool is_add_action)
      : root_(root), presets_owned_(is_add_action),
        is_add_action_(is_add_action) {
  }

  virtual ~UndoablePresetAction() {
    presets_.clear(presets_owned_);
  }

  void AddPreset(PresetItem* p) {
    presets_.add(p);
  }

  int presetCount() const {
    return presets_.size();
  }

 private:
  void addPresetsToTree() {
    DBG(__FUNCTION__ << " preset count: " << presets_.size());
    jassert(presets_owned_);
    for (PresetItem* p : presets_)
      root_->addSubItem(p);
    presets_owned_ = false;
    refreshTree(root_, true);
    root_->getOwnerView()->scrollToKeepItemVisible(presets_[0]);
  }

  void removePresetsFromTree() {
    DBG(__FUNCTION__ << " preset count: " << presets_.size());
    jassert(!presets_owned_);
    int count = root_->getNumSubItems();
    for (int i = count - 1; i >= 0; --i) {
      if (presets_.indexOf(root_->getPreset(i)) != -1) {
        root_->removeSubItem(i, false);
      }
    }
    presets_owned_ = true;
    refreshTree(root_, false);
  }

  virtual bool perform() {
    is_add_action_ ? addPresetsToTree() : removePresetsFromTree();
    return true;
  }

  virtual bool undo() {
    is_add_action_ ? removePresetsFromTree() : addPresetsToTree();
    return true;
  }

  virtual int getSizeInUnits() {
    // TODO: We can do a much better job.
    return presets_.size() * sizeof(PresetItem);
  }

  TreeRootItem* root_;
  bool presets_owned_;
  const bool is_add_action_;
  OwnedArray<PresetItem> presets_;

  DISALLOW_COPY_AND_ASSIGN(UndoablePresetAction);
};

class UndoablePresetIdAction : public juce::UndoableAction {
 public:
  UndoablePresetIdAction(TreeRootItem* root) : root_(root) {}
  virtual ~UndoablePresetIdAction() {}

  void AddPreset(PresetItem* p, int new_id) {
    presets_.add(new PresetIdChange(p, new_id));
  }

  int presetCount() const { return presets_.size(); }

 private:
  void SwitchIds(bool switch_to_new) {
    DBG(__FUNCTION__ << " preset count: " << presets_.size());
    for (PresetIdChange* p : presets_)
      p->preset->set_id(switch_to_new ? p->new_id : p->original_id);
    refreshTree(root_, true);
    root_->getOwnerView()->scrollToKeepItemVisible(presets_[0]->preset);
  }

  virtual bool perform() {
    SwitchIds(true);
    return true;
  }

  virtual bool undo() {
    SwitchIds(false);
    return true;
  }

  virtual int getSizeInUnits() {
    return presets_.size() * sizeof(PresetIdChange);
  }

  struct PresetIdChange {
    PresetIdChange(PresetItem* p, int new_id)
        : preset(p), original_id(p->id()), new_id(new_id) {}

    PresetItem* preset;
    const int original_id;
    const int new_id;

   private:
    DISALLOW_COPY_AND_ASSIGN(PresetIdChange);
  };

  TreeRootItem* root_;
  OwnedArray<PresetIdChange> presets_;

  DISALLOW_COPY_AND_ASSIGN(UndoablePresetIdAction);
};

}  // namespace

TreeRootItem::TreeRootItem(juce::FileDragAndDropTarget* delegate,
                           juce::UndoManager* undo_manager,
                           bool allow_drag_drop_of_presets,
                           bool allow_edit_buffer_presets)
    : undo_manager_(undo_manager),
      delegate_(delegate),
      allow_drag_drop_of_presets_(allow_drag_drop_of_presets),
      allow_edit_buffer_presets_(allow_edit_buffer_presets) {
}

TreeRootItem::~TreeRootItem() {}

void TreeRootItem::sortPresets() {
  ComparePresets comparator;
  sortSubItems(comparator);
}

int TreeRootItem::addPresetsFromFile(const File& file, String* err) {
  jassert(err);
  if (!file.existsAsFile()) {
    *err = "File not found: " + file.getFullPathName();
    return -1;
  }

  MemoryBlock mem;
  if (!file.loadFileAsData(mem)) {
    *err = "Failed to open file: " + file.getFullPathName();
    return -1;
  }

  SysExParser parser;
  auto data = reinterpret_cast<const uint8_t*>(mem.getData());
  if (!parser.ParseSysExBuffer(data, data + mem.getSize(), true)) {
    *err = "Failed to parse file: " + file.getFullPathName();
    return -1;
  }

  // TODO: Support IR and firmware files.
  if (parser.type() != SysExParser::PRESET &&
      parser.type() != SysExParser::PRESET_ARCHIVE) {
    *err = "Not a preset file: " + file.getFullPathName();
    return -1;
  }

  DBG("Parsed file: " + file.getFullPathName());

  // TODO: Detect duplicates.
  //  - only one preset per ID should be allowed.
  //  - Ignore adding a preset that already exists (exact match)
  //  - When a preset is dropped at a specific location in the tree,
  //    ask if the ID of that preset should be changed.
  //

  bool presets_added = false;
  int failed_add_count = 0;

  unique_ptr<UndoablePresetAction> action(new UndoablePresetAction(this, true));

  const auto& presets = parser.presets();
  for (auto& p : presets) {
    //  We don't support system data banks.
    const auto& preset = p.second;
    bool add = !preset->is_global_setting();
    if (add && !allow_edit_buffer_presets_)
      add = !preset->from_edit_buffer();
    if (add) {
      action->AddPreset(new PresetItem(preset));
      presets_added = true;
    } else {
      ++failed_add_count;
    }
  }

  if (!presets_added) {
    if (failed_add_count) {
      *err = "Could not add presets from this file: " + file.getFullPathName() +
             "\nEither the file contains system data or preset(s) without an ID.";
    } else {
      *err = "No presets were found in this file :-|";
    }
    return -1;
  }

  undo_manager_->beginNewTransaction();
  undo_manager_->perform(action.release(), "Add presets");

  return true;
}

PresetItem* TreeRootItem::getPreset(int index) const {
  return static_cast<PresetItem*>(getSubItem(index));
}

void TreeRootItem::deleteSelection() {
  unique_ptr<UndoablePresetAction> action(
      new UndoablePresetAction(this, false));
  int count = getNumSubItems();
  for (int i = count - 1; i >= 0; --i) {
    auto* p = getPreset(i);
    if (p->isSelected())
      action->AddPreset(p);
  }

  if (action->presetCount()) {
    undo_manager_->beginNewTransaction();
    undo_manager_->perform(action.release(), "Remove presets");
  }
}

void TreeRootItem::moveSelectionUp() {
  if (!allow_drag_drop_of_presets_)
    return;

  unique_ptr<UndoablePresetIdAction> action(new UndoablePresetIdAction(this));
  int count = getNumSubItems();
  PresetItem* previous = NULL;
  for (int i = 0; i < count; ++i) {
    auto* p = getPreset(i);
    if (previous && p->isSelected() && !previous->isSelected()) {
      int new_id = previous->id();
      do {
        action->AddPreset(p, new_id++);
        ++i;
        if (i < count)
          p = getPreset(i);
      } while (i < count && p->isSelected());
      action->AddPreset(previous, new_id);
    }
    previous = p;
  }

  if (action->presetCount()) {
    undo_manager_->beginNewTransaction();
    undo_manager_->perform(action.release(), "Move presets");
  }
}

void TreeRootItem::moveSelectionDown() {
  if (!allow_drag_drop_of_presets_)
    return;

  unique_ptr<UndoablePresetIdAction> action(new UndoablePresetIdAction(this));
  int count = getNumSubItems();
  PresetItem* previous = NULL;
  for (int i = count - 1; i >= 0; --i) {
    auto* p = getPreset(i);
    if (previous && p->isSelected() && !previous->isSelected()) {
      int new_id = previous->id();
      do {
        action->AddPreset(p, new_id--);
        --i;
        if (i >= 0)
          p = getPreset(i);
      } while (i >= 0 && p->isSelected());
      action->AddPreset(previous, new_id);
    }
    previous = p;
  }

  if (action->presetCount()) {
    undo_manager_->beginNewTransaction();
    undo_manager_->perform(action.release(), "Move presets");
  }
}

bool TreeRootItem::isInterestedInDragSource(
    const juce::DragAndDropTarget::SourceDetails& source_details) {
  DBG(__FUNCTION__);

  if (!source_details.description.isInt() ||
      static_cast<int>(source_details.description) !=
          PresetItem::kMagicPresetNumber) {
    return false;
  }

  return allow_drag_drop_of_presets_;
}

void TreeRootItem::itemDropped(
    const juce::DragAndDropTarget::SourceDetails& source_details,
    int insert_index) {
  DBG(__FUNCTION__ << " " << insert_index);
  jassert(source_details.description.isInt());
  jassert(static_cast<int>(source_details.description) ==
          PresetItem::kMagicPresetNumber);

  Array<PresetItem*> selected;
  PresetItem* preset = NULL;

  unique_ptr<UndoablePresetIdAction> action(
      new UndoablePresetIdAction(this));

  // Process all selected items _before_ the insert point.
  for (int i = 0; i < insert_index; ++i) {
    preset = getPreset(i);
    if (preset->isSelected())
      selected.add(preset);

    if (selected.size())
      action->AddPreset(preset, preset->id() - selected.size());
  }

  int new_id = preset ? preset->id() + 1 : 0;
  for (auto* n : selected)
    n->set_id(new_id++);
  selected.clear();

  int count = getNumSubItems();
  jassert(count > 0);  // otherwise we shouldn't have reached here.

  for (int i = count - 1; i >= insert_index; --i) {
    preset = getPreset(i);
    if (preset->isSelected())
      selected.add(preset);

    if (selected.size())
      action->AddPreset(preset, preset->id() + selected.size());
  }

  // Add the currently selected items in reverse order.
  new_id += selected.size();
  for (auto* n : selected)
    action->AddPreset(n, --new_id);

  if (action->presetCount()) {
    undo_manager_->beginNewTransaction();
    undo_manager_->perform(action.release(), "Rearrange presets");
  }
}

bool TreeRootItem::keyPressed(const KeyPress& key,
                              Component* originatingComponent) {
  jassert(originatingComponent == getOwnerView());

  if (isDeleteKey(key)) {
    deleteSelection();
    return true;
  }

  if (isMoveUpKey(key)) {
    moveSelectionUp();
    return true;
  }

  if (isMoveDownKey(key)) {
    moveSelectionDown();
    return true;
  }

  if (isUndoKey(key)) {
    undo_manager_->undo();
    return true;
  }

  if (isRedoKey(key)) {
    undo_manager_->redo();
    return true;
  }

  return false;
}
