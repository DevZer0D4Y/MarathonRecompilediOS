#pragma once
#include <filesystem>
#include <functional>
#include <list>
#include <string>

namespace ios
{
    using PickerCompletion = std::function<void(std::list<std::filesystem::path>, std::string)>;
    void PickDocuments(bool folders, PickerCompletion completion);
    void ReleasePickedDocuments();
}
