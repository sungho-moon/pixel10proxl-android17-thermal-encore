#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

using rapidjson::Document;
using rapidjson::Value;

static std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("open_input");
    std::ostringstream out;
    out << in.rdbuf();
    if (!in.good() && !in.eof()) throw std::runtime_error("read_input");
    return out.str();
}

static Value& one_sensor(Document& doc, const char* name) {
    if (!doc.IsObject() || !doc.HasMember("Sensors") || !doc["Sensors"].IsArray())
        throw std::runtime_error("sensors_missing");
    Value* found = nullptr;
    int count = 0;
    for (auto& sensor : doc["Sensors"].GetArray()) {
        if (sensor.IsObject() && sensor.HasMember("Name") && sensor["Name"].IsString()
            && std::string(sensor["Name"].GetString()) == name) {
            found = &sensor;
            ++count;
        }
    }
    if (count != 1 || found == nullptr)
        throw std::runtime_error(std::string("sensor_count:") + name + ":" + std::to_string(count));
    return *found;
}

static Value& one_cdev(Value& sensor, const char* request) {
    if (!sensor.HasMember("BindedCdevInfo") || !sensor["BindedCdevInfo"].IsArray())
        throw std::runtime_error(std::string("cdev_array_missing:") + request);
    Value* found = nullptr;
    int count = 0;
    for (auto& cdev : sensor["BindedCdevInfo"].GetArray()) {
        if (cdev.IsObject() && cdev.HasMember("CdevRequest") && cdev["CdevRequest"].IsString()
            && std::string(cdev["CdevRequest"].GetString()) == request) {
            found = &cdev;
            ++count;
        }
    }
    if (count != 1 || found == nullptr)
        throw std::runtime_error(std::string("cdev_count:") + request + ":" + std::to_string(count));
    return *found;
}

static Value& number_array(Value& object, const char* key, size_t required) {
    if (!object.HasMember(key) || !object[key].IsArray() || object[key].Size() < required)
        throw std::runtime_error(std::string("array_shape:") + key);
    Value& array = object[key];
    for (size_t i = 0; i < required; ++i) {
        if (i == 0 && array[static_cast<rapidjson::SizeType>(i)].IsString()) continue;
        if (!array[static_cast<rapidjson::SizeType>(i)].IsNumber())
            throw std::runtime_error(std::string("array_value:") + key + ":" + std::to_string(i));
    }
    return array;
}

static void threshold(Value& sensor, size_t index, double value) {
    if (!sensor.HasMember("HotThreshold") || !sensor["HotThreshold"].IsArray()
        || sensor["HotThreshold"].Size() <= index) return;
    Value& array = sensor["HotThreshold"];
    if (!array[static_cast<rapidjson::SizeType>(index)].IsNumber()) return;
    double old = array[static_cast<rapidjson::SizeType>(index)].GetDouble();
    if (old >= 25.0 && old <= 60.0) array[static_cast<rapidjson::SizeType>(index)].SetDouble(value);
}

static void polling(Value& sensor) {
    if (!sensor.HasMember("PollingDelay") || !sensor["PollingDelay"].IsInt64())
        throw std::runtime_error("polling_missing");
    long long old = sensor["PollingDelay"].GetInt64();
    if (old < 1000 || old > 600000) throw std::runtime_error("polling_range");
    sensor["PollingDelay"].SetInt(5000);
}

static void levels(Value& sensor, const char* request, const char* key,
                   const std::vector<size_t>& indexes, long long target) {
    Value& cdev = one_cdev(sensor, request);
    if (!cdev.HasMember(key) || !cdev[key].IsArray())
        throw std::runtime_error(std::string("array_shape:") + key);
    Value& array = cdev[key];
    size_t changed = 0;
    for (size_t i : indexes) {
        if (i >= array.Size()) continue;
        Value& slot = array[static_cast<rapidjson::SizeType>(i)];
        if (!slot.IsInt64() && !slot.IsUint64()) continue;
        long long old = slot.IsInt64() ? slot.GetInt64() : static_cast<long long>(slot.GetUint64());
        if (old <= 0 || old > 5000000000LL) continue;
        slot.SetInt64(target);
        ++changed;
    }
    if (changed == 0) throw std::runtime_error(std::string("array_shape:") + key);
}

static int apply_profile(Document& doc) {
    Value& hint = one_sensor(doc, "VIRTUAL-SKIN-HINT");
    Value& light = one_sensor(doc, "VIRTUAL-SKIN-CPU-LIGHT-ODPM");
    Value& mid = one_sensor(doc, "VIRTUAL-SKIN-CPU-MID");
    Value& odpm = one_sensor(doc, "VIRTUAL-SKIN-CPU-ODPM");
    Value& high = one_sensor(doc, "VIRTUAL-SKIN-CPU-HIGH");
    Value& soc = one_sensor(doc, "VIRTUAL-SKIN-SOC");

    threshold(hint, 1, 39.0); polling(hint);

    threshold(light, 1, 40.0); threshold(light, 2, 42.0); polling(light);
    levels(light, "cpufreq-cpu0", "CdevCeilingFrequency", {1, 2}, 1881000);
    levels(light, "cpufreq-cpu2", "CdevCeilingFrequency", {1, 2}, 2534000);
    levels(light, "big_and_big_mid", "CdevCeiling", {1, 2}, 4);

    threshold(mid, 1, 42.0); threshold(mid, 2, 44.0); polling(mid);
    levels(mid, "thermal-uclamp-0", "CdevCeilingFrequency", {1, 2}, 1881000);
    levels(mid, "thermal-uclamp-2", "CdevCeilingFrequency", {1, 2}, 2534000);
    levels(mid, "thermal-uclamp-5", "CdevCeilingFrequency", {1, 2}, 2534000);
    levels(mid, "thermal-uclamp-7", "CdevCeilingFrequency", {1, 2}, 2937000);

    threshold(odpm, 1, 42.0); threshold(odpm, 2, 44.0); polling(odpm);
    levels(odpm, "cpufreq-cpu0", "CdevCeilingFrequency", {1, 2}, 1881000);
    levels(odpm, "cpufreq-cpu2", "CdevCeilingFrequency", {1, 2}, 2534000);
    levels(odpm, "big_and_big_mid", "CdevCeiling", {1, 2}, 4);

    threshold(high, 1, 44.0); threshold(high, 2, 46.0); polling(high);

    levels(soc, "cpufreq-cpu0", "CdevCeilingFrequency", {1, 2, 3}, 1881000);
    levels(soc, "cpufreq-cpu2", "CdevCeilingFrequency", {1, 2, 3}, 2534000);
    levels(soc, "big_and_big_mid", "CdevCeiling", {1, 2, 3}, 4);
    // Keep the 748 MHz ceiling through the first high-temperature states.
    // Derate in two smaller steps only at the upper protection states.
    levels(soc, "gpu", "CdevCeilingFrequency", {1, 2, 3, 4}, 748000000);
    levels(soc, "gpu", "CdevCeilingFrequency", {5}, 633000000);
    levels(soc, "gpu", "CdevCeilingFrequency", {6}, 512000000);
    return 40;
}

static void atomic_write(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp." + std::to_string(getpid());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("open_output");
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) throw std::runtime_error("write_output");
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        throw std::runtime_error("rename_output");
    }
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: thermal-profile-builder probe INPUT | patch INPUT OUTPUT\n";
        return 2;
    }
    try {
        std::string text = read_all(argv[2]);
        Document doc;
        doc.Parse(text.data(), text.size());
        if (doc.HasParseError()) throw std::runtime_error("json_parse");
        int changes = apply_profile(doc);
        if (std::string(argv[1]) == "probe") {
            if (argc != 3) return 2;
            std::cout << "THERMAL_PROFILE_MATCH changes=" << changes << "\n";
            return 0;
        }
        if (std::string(argv[1]) != "patch" || argc != 4) return 2;
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.SetIndent(' ', 4);
        doc.Accept(writer);
        std::string output(buffer.GetString(), buffer.GetSize());
        output.push_back('\n');
        atomic_write(argv[3], output);
        std::cout << "THERMAL_PROFILE_PATCHED changes=" << changes << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "THERMAL_PROFILE_REJECT reason=" << error.what() << "\n";
        return 3;
    }
}

