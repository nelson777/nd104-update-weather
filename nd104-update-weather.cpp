#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

const std::string DEFAULT_LOCATION = "Fortaleza, Brazil";
const std::string DEFAULT_HID_NAME_REQUIRED_STRINGS = "nd104,screen";
const std::string UPDATER_VERSION = "0.3.9Ca";

const std::vector<int> WEATHERAPI_CODES = {
    1000,
    1003,
    1006,
    1009,
    1030,
    1063,
    1066,
    1069,
    1072,
    1087,
    1114,
    1117,
    1135,
    1147,
    1150,
    1153,
    1168,
    1171,
    1180,
    1183,
    1186,
    1189,
    1192,
    1195,
    1198,
    1201,
    1204,
    1207,
    1210,
    1213,
    1216,
    1219,
    1222,
    1225,
    1237,
    1240,
    1243,
    1246,
    1249,
    1252,
    1255,
    1258,
    1261,
    1264,
    1273,
    1276,
    1279,
    1282,
};

const std::vector<uint8_t> BASE_PAYLOAD = {
    0x1c, 0x02, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x01,
    0xa5, 0xfe, 0x00, 0x08, 0x00, 0x02, 0x01, 0x0e,
    0x01, 0x26, 0x00, 0xed, 0xd4, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

struct HidDevice {
    std::string device;
    std::string name;
    std::string hidId;
    std::string vendorId;
    std::string productId;
    std::string uniq;
    std::string phys;
    std::string driver;
};

struct WeatherData {
    std::string location;
    std::string lastUpdated;
    std::string conditionText;
    int conditionCode;
    double tempC;
    double maxTempC;
    double minTempC;
};

std::string getEnvOrDefault(const std::string& name, const std::string& defaultValue) {
    const char* value = std::getenv(name.c_str());
    if (!value || std::strlen(value) == 0) {
        return defaultValue;
    }
    return value;
}

std::string getRequiredEnv(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    if (!value || std::strlen(value) == 0) {
        throw std::runtime_error("Erro: defina " + name);
    }
    return value;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }

    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::vector<std::string> splitCommaList(const std::string& value) {
    std::vector<std::string> result;
    std::stringstream ss(value);
    std::string item;

    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            result.push_back(toLower(item));
        }
    }

    return result;
}

std::string readTextFile(const fs::path& filePath) {
    std::ifstream file(filePath);

    if (!file) {
        throw std::runtime_error("Não foi possível ler: " + filePath.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::map<std::string, std::string> parseUevent(const std::string& text) {
    std::map<std::string, std::string> result;
    std::stringstream ss(text);
    std::string line;

    while (std::getline(ss, line)) {
        size_t pos = line.find('=');

        if (pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        result[key] = value;
    }

    return result;
}

std::vector<std::string> splitColon(const std::string& value) {
    std::vector<std::string> result;
    std::stringstream ss(value);
    std::string item;

    while (std::getline(ss, item, ':')) {
        result.push_back(item);
    }

    return result;
}

std::string normalizeUsbId(std::string hex) {
    while (hex.size() > 1 && hex[0] == '0') {
        hex.erase(hex.begin());
    }

    while (hex.size() < 4) {
        hex = "0" + hex;
    }

    return toLower(hex);
}

bool containsAllRequiredStrings(
    const std::string& hidName,
    const std::vector<std::string>& requiredStrings
) {
    std::string nameLower = toLower(hidName);

    for (const auto& required : requiredStrings) {
        if (nameLower.find(required) == std::string::npos) {
            return false;
        }
    }

    return true;
}

HidDevice findNd104ScreenHidraw(const std::vector<std::string>& requiredStrings) {
    const fs::path hidrawSysfs = "/sys/class/hidraw";

    if (!fs::exists(hidrawSysfs)) {
        throw std::runtime_error("/sys/class/hidraw não existe");
    }

    for (const auto& entry : fs::directory_iterator(hidrawSysfs)) {
        std::string hidrawName = entry.path().filename().string();

        try {
            fs::path devicePath = fs::canonical(entry.path() / "device");
            fs::path ueventPath = devicePath / "uevent";

            std::string ueventText = readTextFile(ueventPath);
            auto info = parseUevent(ueventText);

            std::string hidName = info.count("HID_NAME") ? info["HID_NAME"] : "";

            if (!containsAllRequiredStrings(hidName, requiredStrings)) {
                continue;
            }

            std::string hidId = info.count("HID_ID") ? info["HID_ID"] : "";
            auto parts = splitColon(hidId);

            if (parts.size() != 3) {
                throw std::runtime_error("Dispositivo encontrado, mas HID_ID inválido: " + hidId);
            }

            HidDevice dev;
            dev.device = "/dev/" + hidrawName;
            dev.name = hidName;
            dev.hidId = hidId;
            dev.vendorId = normalizeUsbId(parts[1]);
            dev.productId = normalizeUsbId(parts[2]);
            dev.uniq = info.count("HID_UNIQ") ? info["HID_UNIQ"] : "";
            dev.phys = info.count("HID_PHYS") ? info["HID_PHYS"] : "";
            dev.driver = info.count("DRIVER") ? info["DRIVER"] : "";

            return dev;
        } catch (...) {
            continue;
        }
    }

    std::stringstream msg;
    msg << "Nenhum hidraw encontrado com HID_NAME contendo todas estas strings: ";

    for (size_t i = 0; i < requiredStrings.size(); ++i) {
        if (i > 0) {
            msg << ", ";
        }
        msg << requiredStrings[i];
    }

    throw std::runtime_error(msg.str());
}

void setUInt16BE(std::vector<uint8_t>& buf, size_t offset, int value) {
    uint16_t unsignedValue = static_cast<uint16_t>(value & 0xffff);

    buf[offset] = static_cast<uint8_t>((unsignedValue >> 8) & 0xff);
    buf[offset + 1] = static_cast<uint8_t>(unsignedValue & 0xff);
}

void setTempC(std::vector<uint8_t>& buf, size_t offset, double tempC) {
    int tenths = static_cast<int>(std::round(tempC * 10.0));
    setUInt16BE(buf, offset, tenths);
}

void updateChecksum(std::vector<uint8_t>& buf) {
    uint8_t sum = 0;

    for (size_t i = 0x0a; i <= 0x13; ++i) {
        sum = static_cast<uint8_t>((sum + buf[i]) & 0xff);
    }

    buf[0x14] = static_cast<uint8_t>((0x01 - sum) & 0xff);
}

bool verifyChecksum(const std::vector<uint8_t>& buf) {
    uint8_t sum = 0;

    for (size_t i = 0x0a; i <= 0x14; ++i) {
        sum = static_cast<uint8_t>((sum + buf[i]) & 0xff);
    }

    return sum == 0x01;
}

int weatherApiCodeToNd104Code(int weatherApiCode) {
    auto it = std::find(WEATHERAPI_CODES.begin(), WEATHERAPI_CODES.end(), weatherApiCode);

    if (it == WEATHERAPI_CODES.end()) {
        throw std::runtime_error("Código WeatherAPI desconhecido: " + std::to_string(weatherApiCode));
    }

    int index = static_cast<int>(std::distance(WEATHERAPI_CODES.begin(), it));

    if (index > 0xff) {
        throw std::runtime_error("Índice de clima fora de 1 byte: " + std::to_string(index));
    }

    return index;
}

size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    auto* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::string urlEncode(CURL* curl, const std::string& value) {
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));

    if (!encoded) {
        throw std::runtime_error("Falha ao codificar URL");
    }

    std::string result(encoded);
    curl_free(encoded);

    return result;
}

WeatherData getWeather(const std::string& apiKey, const std::string& location) {
    CURL* curl = curl_easy_init();

    if (!curl) {
        throw std::runtime_error("Falha ao inicializar libcurl");
    }

    std::string responseBody;

    try {
        std::string url =
            "https://api.weatherapi.com/v1/forecast.json"
            "?key=" + urlEncode(curl, apiKey) +
            "&q=" + urlEncode(curl, location) +
            "&days=1"
            "&aqi=no"
            "&alerts=no";

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::string error = curl_easy_strerror(res);
            curl_easy_cleanup(curl);
            throw std::runtime_error("Erro libcurl: " + error);
        }

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);

        if (httpCode < 200 || httpCode >= 300) {
            throw std::runtime_error(
                "WeatherAPI HTTP " + std::to_string(httpCode) + ": " + responseBody
            );
        }

        json data = json::parse(responseBody);

        if (data.contains("error")) {
            int code = data["error"].value("code", 0);
            std::string message = data["error"].value("message", "");
            throw std::runtime_error(
                "WeatherAPI erro " + std::to_string(code) + ": " + message
            );
        }

        auto current = data.at("current");
        auto day = data.at("forecast").at("forecastday").at(0).at("day");

        WeatherData weather;
        weather.location =
            data.at("location").value("name", location) + ", " +
            data.at("location").value("country", "");

        weather.lastUpdated = current.value("last_updated", "");
        weather.conditionText = current.at("condition").value("text", "");
        weather.conditionCode = current.at("condition").value("code", 0);
        weather.tempC = current.value("temp_c", 0.0);
        weather.maxTempC = day.value("maxtemp_c", 0.0);
        weather.minTempC = day.value("mintemp_c", 0.0);

        return weather;
    } catch (...) {
        curl_easy_cleanup(curl);
        throw;
    }
}

void writePayloadToDevice(const std::string& devicePath, const std::vector<uint8_t>& payload) {
    int fd = open(devicePath.c_str(), O_WRONLY);

    if (fd < 0) {
        throw std::runtime_error(
            "Não foi possível abrir " + devicePath + ": " + std::strerror(errno)
        );
    }

    ssize_t written = write(fd, payload.data(), payload.size());

    close(fd);

    if (written < 0) {
        throw std::runtime_error(
            "Erro ao escrever em " + devicePath + ": " + std::strerror(errno)
        );
    }

    if (static_cast<size_t>(written) != payload.size()) {
        throw std::runtime_error(
            "Escrita incompleta em " + devicePath + ": " +
            std::to_string(written) + "/" + std::to_string(payload.size()) + " bytes"
        );
    }
}

std::string payloadToHex(const std::vector<uint8_t>& payload) {
    const char* hexChars = "0123456789abcdef";
    std::string result;

    for (size_t i = 0; i < payload.size(); ++i) {
        if (i > 0) {
            result += ' ';
        }

        uint8_t b = payload[i];
        result += hexChars[(b >> 4) & 0x0f];
        result += hexChars[b & 0x0f];
    }

    return result;
}

int main() {
    try {
        std::string apiKey = getRequiredEnv("WEATHER_API_KEY");
        std::string location = getEnvOrDefault("WEATHER_LOCATION", DEFAULT_LOCATION);

        std::string requiredStringsEnv = getEnvOrDefault(
            "ND104_HID_NAME_REQUIRED_STRINGS",
            DEFAULT_HID_NAME_REQUIRED_STRINGS
        );

        auto requiredStrings = splitCommaList(requiredStringsEnv);

        if (requiredStrings.empty()) {
            throw std::runtime_error("ND104_HID_NAME_REQUIRED_STRINGS não pode ser vazio");
        }

        curl_global_init(CURL_GLOBAL_DEFAULT);

        HidDevice nd104 = findNd104ScreenHidraw(requiredStrings);
        WeatherData weather = getWeather(apiKey, location);

        int nd104WeatherCode = weatherApiCodeToNd104Code(weather.conditionCode);

        std::vector<uint8_t> payload = BASE_PAYLOAD;

        // Estrutura conhecida:
        //
        // 0x0b      = não alterar
        // 0x0c      = não alterar
        // 0x0d      = índice do código WeatherAPI na tabela
        // 0x0e-0x0f = temperatura atual ×10, big-endian
        // 0x10-0x11 = temperatura máxima ×10, big-endian
        // 0x12-0x13 = temperatura mínima ×10, big-endian
        // 0x14      = checksum
        payload[0x0d] = static_cast<uint8_t>(nd104WeatherCode & 0xff);

        setTempC(payload, 0x0e, weather.tempC);
        setTempC(payload, 0x10, weather.maxTempC);
        setTempC(payload, 0x12, weather.minTempC);

        updateChecksum(payload);

        if (!verifyChecksum(payload)) {
            throw std::runtime_error("Checksum inválido");
        }

        writePayloadToDevice(nd104.device, payload);

        std::cout << "Atualizado:\n";
        std::cout << "  device: " << nd104.device << "\n";
        std::cout << "  detectedName: " << nd104.name << "\n";
        std::cout << "  detectedVendorId: " << nd104.vendorId << "\n";
        std::cout << "  detectedProductId: " << nd104.productId << "\n";
        std::cout << "  detectedHidId: " << nd104.hidId << "\n";
        std::cout << "  location: " << weather.location << "\n";
        std::cout << "  lastUpdated: " << weather.lastUpdated << "\n";
        std::cout << "  condition: " << weather.conditionText << "\n";
        std::cout << "  weatherApiCode: " << weather.conditionCode << "\n";
        std::cout << "  nd104WeatherCode: " << nd104WeatherCode << "\n";
        std::cout << "  tempC: " << weather.tempC << "\n";
        std::cout << "  maxTempC: " << weather.maxTempC << "\n";
        std::cout << "  minTempC: " << weather.minTempC << "\n";
        std::cout << "  payload: " << payloadToHex(payload) << "\n";
        std::cout << "  updaterVersion: " << UPDATER_VERSION << "\n";

        curl_global_cleanup();
        return 0;
    } catch (const std::exception& e) {
        curl_global_cleanup();
        std::cerr << e.what() << "\n";
        return 1;
    }
}
