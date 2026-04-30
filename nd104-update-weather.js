#!/usr/bin/env node

const fs = require('node:fs');
const path = require('node:path');

const UPDATER_VERSION='0.3.12a'
const API_KEY = process.env.WEATHER_API_KEY;
const LOCATION = process.env.WEATHER_LOCATION || 'Fortaleza, Brazil';

const HID_NAME_REQUIRED_STRINGS = (
  process.env.ND104_HID_NAME_REQUIRED_STRINGS || 'nd104,screen'
)
  .split(',')
  .map((value) => value.trim())
  .filter(Boolean);

if (!API_KEY) {
  console.error('Erro: defina WEATHER_API_KEY');
  console.error('Exemplo: sudo -E WEATHER_API_KEY="sua_chave" ./nd104-weather.js');
  process.exit(1);
}

// Ordem oficial da tabela WeatherAPI.
// O ND104 usa o índice dessa sequência:
// 00 -> 1000 -> Sunny
// 01 -> 1003 -> Partly cloudy
// 02 -> 1006 -> Cloudy
// ...
const WEATHERAPI_CODES = [
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
];

// Payload base validado no ND104.
const BASE_PAYLOAD = Buffer.from([
  0x1c, 0x02, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x01,
  0xa5, 0xfe, 0x00, 0x08, 0x00, 0x02, 0x01, 0x0e,
  0x01, 0x26, 0x00, 0xed, 0xd4, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
]);

function parseUevent(text) {
  const result = {};

  for (const line of text.trim().split('\n')) {
    const [key, ...value] = line.split('=');
    result[key] = value.join('=');
  }

  return result;
}

function normalizeUsbId(hex) {
  return hex.replace(/^0+/, '').padStart(4, '0').toLowerCase();
}

function findNd104ScreenHidraw() {
  const HIDRAW_SYSFS = '/sys/class/hidraw';

  const entries = fs.readdirSync(HIDRAW_SYSFS);

  for (const entry of entries) {
    let sysDevicePath;
    let ueventText;

    try {
      sysDevicePath = fs.realpathSync(path.join(HIDRAW_SYSFS, entry, 'device'));
      ueventText = fs.readFileSync(path.join(sysDevicePath, 'uevent'), 'utf8');
    } catch {
      continue;
    }

    const info = parseUevent(ueventText);

    const hidName = info.HID_NAME || '';
    const hidNameLower = hidName.toLowerCase();

    const hasAllRequiredStrings = HID_NAME_REQUIRED_STRINGS.every((requiredString) =>
      hidNameLower.includes(requiredString.toLowerCase())
    );

    if (!hasAllRequiredStrings) {
      continue;
    }

    const hidId = info.HID_ID || '';

    // Exemplo:
    // HID_ID=0003:00005542:00000001
    const [, vendorHex, productHex] = hidId.split(':');

    if (!vendorHex || !productHex) {
      throw new Error(`Dispositivo encontrado, mas HID_ID inválido: ${hidId}`);
    }

    return {
      device: `/dev/${entry}`,
      name: hidName,
      hidId,
      vendorId: normalizeUsbId(vendorHex),
      productId: normalizeUsbId(productHex),
      uniq: info.HID_UNIQ || '',
      phys: info.HID_PHYS || '',
      driver: info.DRIVER || '',
    };
  }

  throw new Error(
    `Nenhum hidraw encontrado com HID_NAME contendo todas estas strings: ${HID_NAME_REQUIRED_STRINGS.join(', ')}`
  );
}

function setUInt16BE(buf, offset, value) {
  const unsigned = value & 0xffff;

  buf[offset] = (unsigned >> 8) & 0xff;
  buf[offset + 1] = unsigned & 0xff;
}

function setTempC(buf, offset, tempC) {
  const tenths = Math.round(tempC * 10);
  setUInt16BE(buf, offset, tenths);
}

function updateChecksum(buf) {
  let sum = 0;

  for (let i = 0x0a; i <= 0x13; i++) {
    sum = (sum + buf[i]) & 0xff;
  }

  buf[0x14] = (0x01 - sum) & 0xff;
}

function verifyChecksum(buf) {
  let sum = 0;

  for (let i = 0x0a; i <= 0x14; i++) {
    sum = (sum + buf[i]) & 0xff;
  }

  return sum === 0x01;
}

function weatherApiCodeToNd104Code(weatherApiCode) {
  const index = WEATHERAPI_CODES.indexOf(weatherApiCode);

  if (index < 0) {
    throw new Error(`Código WeatherAPI desconhecido: ${weatherApiCode}`);
  }

  if (index > 0xff) {
    throw new Error(`Índice de clima fora de 1 byte: ${index}`);
  }

  return index;
}

function parseWeatherApiTime(lastUpdated) {
  const match = String(lastUpdated).match(/\b(\d{2}):(\d{2})\b/);

  if (!match) {
    const now = new Date();

    return {
      hour: now.getHours(),
      minute: now.getMinutes(),
    };
  }

  return {
    hour: Number(match[1]),
    minute: Number(match[2]),
  };
}

async function getWeather() {
  const url = new URL('https://api.weatherapi.com/v1/forecast.json');

  url.searchParams.set('key', API_KEY);
  url.searchParams.set('q', LOCATION);
  url.searchParams.set('days', '1');
  url.searchParams.set('aqi', 'no');
  url.searchParams.set('alerts', 'no');

  const response = await fetch(url);

  if (!response.ok) {
    const text = await response.text();
    throw new Error(`WeatherAPI HTTP ${response.status}: ${text}`);
  }

  const data = await response.json();

  if (data.error) {
    throw new Error(`WeatherAPI erro ${data.error.code}: ${data.error.message}`);
  }

  const current = data.current;
  const day = data.forecast?.forecastday?.[0]?.day;

  if (!current || !day) {
    throw new Error('Resposta inválida da WeatherAPI: faltando current ou forecast.forecastday[0].day');
  }

  return {
    location: `${data.location?.name || LOCATION}, ${data.location?.country || ''}`.trim(),
    lastUpdated: current.last_updated,
    conditionText: current.condition?.text,
    conditionCode: current.condition?.code,
    tempC: current.temp_c,
    maxTempC: day.maxtemp_c,
    minTempC: day.mintemp_c,
  };
}

function toBcd(value) {
  if (!Number.isInteger(value) || value < 0 || value > 99) {
    throw new Error(`Valor inválido para BCD: ${value}`);
  }

  return ((Math.floor(value / 10) << 4) | (value % 10)) & 0xff;
}

async function main() {
  const nd104 = findNd104ScreenHidraw();
  const weather = await getWeather();

  const nd104WeatherCode = weatherApiCodeToNd104Code(weather.conditionCode);

  const payload = Buffer.from(BASE_PAYLOAD);

  payload[0x0d] = nd104WeatherCode & 0xff;

  setTempC(payload, 0x0e, weather.tempC);
  setTempC(payload, 0x10, weather.maxTempC);
  setTempC(payload, 0x12, weather.minTempC);

  updateChecksum(payload);

  if (!verifyChecksum(payload)) {
    throw new Error('Checksum inválido');
  }

 fs.writeFileSync(nd104.device, payload);

  console.log('Updated:', {
    device: nd104.device,
    detectedName: nd104.name,
    detectedVendorId: nd104.vendorId,
    detectedProductId: nd104.productId,
    detectedHidId: nd104.hidId,
    requiredNameStrings: HID_NAME_REQUIRED_STRINGS,
    location: weather.location,
    lastUpdated: weather.lastUpdated,
    condition: weather.conditionText,
    weatherApiCode: weather.conditionCode,
    nd104WeatherCode,
    tempC: weather.tempC,
    maxTempC: weather.maxTempC,
    minTempC: weather.minTempC,
    payload: payload.toString('hex').match(/../g).join(' '),
    updaterVersion: UPDATER_VERSION
  });
}

main().catch((err) => {
  console.error(err.message);
  process.exit(1);
});
