#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
using namespace juce;

namespace Service
{
    struct PresetMetadata
    {
        String name;
        String artist;
        String category;
        String dateCreated;
        String dateModified;
        StringArray tags;
        String getFullPath() const { return category.isEmpty() ? name : category + "/" + name; }
    };

    class PresetManager : private ValueTree::Listener, private AudioProcessorParameter::Listener
    {
    public:
        static const File defaultDirectory;
        static const String extension;
        static const String presetNameProperty;
        static const String defaultCategory;

        PresetManager(AudioProcessorValueTreeState&);
        ~PresetManager();

        void buildPresetMenu(PopupMenu& menu, int& menuItemId);
        void handlePresetMenuResult(int result, const StringArray& menuItemIds);

        void savePreset(const String& presetName, const String& artistName = "Unknown", const String& category = "");
        void deletePreset(const String& presetName, const String& category = "");
        void loadPreset(const String& presetName, const String& category = "");

        void createCategory(const String& categoryName);
        void deleteCategory(const String& categoryName);
        bool categoryExists(const String& categoryName) const;
        StringArray getAllCategories() const;

        StringArray getAllPresets() const;
        StringArray getPresetsInCategory(const String& category) const;
        Array<PresetMetadata> getAllPresetMetadata() const;
        Array<PresetMetadata> getPresetMetadataInCategory(const String& category) const;

        int loadNextPreset();
        int loadPreviousPreset();
        int loadNextPresetInCategory(const String& category);
        int loadPreviousPresetInCategory(const String& category);

        String getCurrentPreset() const;
        String getCurrentCategory() const;

        void movePresetToCategory(const String& presetName, const String& fromCategory, const String& toCategory);
        File getCategoryDirectory(const String& category) const;

    private:
        void buildCategorySubmenu(PopupMenu& submenu, const String& category, int& menuItemId);
        StringArray menuItemToPresetMap;
        StringArray menuItemToCategoryMap;

        void valueTreeRedirected(ValueTree& treeWhichHasBeenChanged) override;
        void updatePresetList();
        File getPresetFile(const String& presetName, const String& category) const;

        void parameterValueChanged(int parameterIndex, float newValue) override;
        void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

        void addParameterListeners();
        void removeParameterListeners();

        bool currentStateMatchesPreset() const;

        AudioProcessorValueTreeState& valueTreeState;
        Value currentPreset;
        Value currentCategory;
        StringArray availablePresets;
        StringArray availableCategories;

        bool isLoadingPreset = false;
    };
}
