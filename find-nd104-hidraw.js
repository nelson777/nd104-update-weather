#!/usr/bin/env node

const fs = require('node:fs');
const path = require('node:path');

const TARGET_VENDOR = '00005542';
const TARGET_PRODUCT = '00000001';

const HIDRAW_SYSFS = '/sys/class/hidraw';

function readFileIfExists(file) {
  try {
    return fs.readFileSync(file, 'utf8').trim();
  } catch {
    return null;
  }
}

function parseUevent(text) {
  const result = {};

  for (const line of text.split('\n')) {
    const [key, ...rest] = line.split('=');
    result[key] = rest.join('=');
  }

  return result;
}

function findHidrawDevices() {
  const entries = fs.readdirSync(HIDRAW_SYSFS);

  const matches = [];

  for (const entry of entries) {
    const hidrawPath = `/dev/${entry}`;
    const sysDevicePath = fs.realpathSync(path.join(HIDRAW_SYSFS, entry, 'device'));
    const ueventPath = path.join(sysDevicePath, 'uevent');

    const ueventText = readFileIfExists(ueventPath);

    if (!ueventText) {
      continue;
    }

    const info = parseUevent(ueventText);

    // Exemplo:
    // HID_ID=0003:00005542:00000001
    const hidId = info.HID_ID || '';
    const parts = hidId.split(':');

    if (parts.length !== 3) {
      continue;
    }

    const [, vendor, product] = parts;

    if (
      vendor.toLowerCase() === TARGET_VENDOR.toLowerCase() &&
      product.toLowerCase() === TARGET_PRODUCT.toLowerCase()
    ) {
      matches.push({
        hidraw: hidrawPath,
        name: info.HID_NAME || '',
        uniq: info.HID_UNIQ || '',
        driver: info.DRIVER || '',
        hidId,
        phys: info.HID_PHYS || '',
      });
    }
  }

  return matches;
}

const matches = findHidrawDevices();

if (matches.length === 0) {
  console.error('Nenhum hidraw encontrado para VID:PID 5542:0001');
  process.exit(1);
}

for (const dev of matches) {
  console.log(dev.hidraw);
  console.log(`  name:   ${dev.name}`);
  console.log(`  driver: ${dev.driver}`);
  console.log(`  hid_id: ${dev.hidId}`);
  console.log(`  uniq:   ${dev.uniq}`);
  console.log(`  phys:   ${dev.phys}`);
}
