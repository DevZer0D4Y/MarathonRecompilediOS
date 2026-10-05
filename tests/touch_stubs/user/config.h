#pragma once
namespace Config {
template<class T> struct Setting { T Value; operator T() const { return Value; } Setting& operator=(T value) { Value=value; return *this; } };
inline Setting<bool> TouchControls{true};
inline Setting<float> TouchControlsOpacity{1.0f};
inline int saveCount=0;
inline void Save() { ++saveCount; }
}
