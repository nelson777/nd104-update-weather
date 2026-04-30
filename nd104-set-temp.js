#!/usr/bin/env node

const fs = require('fs');

const device = process.argv[2] || '/dev/hidraw9';

const BASE_HEX = `
1c 02 00 00 00 0d 49 01 a5 fe 00 08 00 02 01 0e
01 26 00 ed d4 00 00 00 00 00 00 00 00 00 00 00
`;

function hexToBuffer(hex) {
  return Buffer.from(hex.replace(/[^0-9a-fA-F]/g, ''), 'hex');
}

function setTempTenths(payload, offset, tempCelsius) {
  const value = Math.round(tempCelsius * 10);

  payload[offset] = (value >> 8) & 0xff;
  payload[offset + 1] = value & 0xff;
}

function updateChecksum(payload) {
  let sum = 0;

  for (let i = 0x0a; i <= 0x13; i++) {
    sum = (sum + payload[i]) & 0xff;
  }

  payload[0x14] = (0x01 - sum) & 0xff;
}

function verifyChecksum(payload) {
  let sum = 0;

  for (let i = 0x0a; i <= 0x14; i++) {
    sum = (sum + payload[i]) & 0xff;
  }

  return sum === 0x01;
}

const payload = hexToBuffer(BASE_HEX);

// Código interno do clima/ícone.
// Valor original era 0x02.
// Novo valor: 0x03.
payload[0x0d] = 0x03;

// Temperaturas em 20.0 °C.
setTempTenths(payload, 0x0e, 20.0); // atual
setTempTenths(payload, 0x10, 20.0); // máxima
setTempTenths(payload, 0x12, 20.0); // mínima

updateChecksum(payload);

if (!verifyChecksum(payload)) {
  throw new Error('Checksum inválido');
}

console.log('Payload:', payload.toString('hex').match(/../g).join(' '));

fs.writeFileSync(device, payload);

console.log(`Enviado para ${device}`);
