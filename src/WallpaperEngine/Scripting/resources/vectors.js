// Script-owned vectors use ordinary JavaScript numbers and fields. Keep arithmetic
// inside the JIT; conversion to the renderer's floats happens at the C++ boundary.
// Borrowed engine values retain their native accessors and lifetime checks.
(function () {
  'use strict';
  const keys = ['x', 'y', 'z', 'w'];
  for (const size of [2, 3, 4]) {
    const Native = globalThis['Vec' + size];
    const proto = Object.create(Native.prototype);
    function Vec(x, y, z, w) {
      if (!new.target) throw new TypeError('Vector constructor requires new');
      let a;
      if (typeof x === 'string') {
        const parts = x.split(' ');
        a = keys.slice(0, size).map((_, i) => parseFloat(parts[i]));
      } else if (x && typeof x === 'object' && (x instanceof Vec || x instanceof Native ||
                 (size !== 2 && x instanceof Vec2) ||
                 ((size === 2 || size === 4) && x instanceof Vec3))) {
        a = [x.x, x.y, typeof x.z === 'number' ? x.z : 0, typeof x.w === 'number' ? x.w : 0];
      } else if (x === undefined) {
        a = [0, 0, 0, 0];
      } else {
        const hasY = typeof y === 'number', hasZ = typeof z === 'number';
        this.x = x; this.y = hasY ? y : x;
        if (size >= 3) this.z = hasZ ? z : hasY ? 0 : x;
        if (size === 4) this.w = typeof w === 'number' ? w : hasZ ? z : hasY ? 0 : x;
        return;
      }
      this.x = a[0]; this.y = a[1];
      if (size >= 3) this.z = a[2];
      if (size === 4) this.w = a[3];
    }
    Object.defineProperty(Vec, 'name', {value: 'Vec' + size});
    Vec.prototype = proto;
    Object.defineProperty(proto, 'constructor', {value: Vec, writable: true, configurable: true});
    function make(a) { return new Vec(...a); }
    function read(v) { return keys.slice(0, size).map(k => v[k]); }
    function method(name, fn) {
      Object.defineProperty(proto, name, {value: fn, writable: true, configurable: true});
    }
    method('copy', function () { return new Vec(this.x, this.y, this.z, this.w); });
    method('equals', function (other) {
      return other instanceof Vec && keys.slice(0, size).every(k => Math.abs(this[k] - other[k]) < .00001);
    });
    method('lengthSqr', function () {
      let sum = this.x * this.x + this.y * this.y;
      if (size >= 3) sum += this.z * this.z;
      if (size === 4) sum += this.w * this.w;
      return sum;
    });
    method('length', function () { return Math.sqrt(this.lengthSqr()); });
    method('normalize', function () { return this.divide(this.length()); });
    for (const [name, op] of Object.entries({add: (a, b) => a + b, subtract: (a, b) => a - b,
        multiply: (a, b) => a * b, divide: (a, b) => a / b, min: Math.min, max: Math.max})) {
      method(name, function (other) {
        const scalar = typeof other === 'number';
        let z = size >= 3 ? (scalar ? other : other.z) : undefined;
        if (size === 3 && other instanceof Vec2) {
          if (name === 'add' || name === 'subtract') z = 0;
          if (name === 'multiply' || name === 'divide') z = 1;
        }
        return new Vec(op(this.x, scalar ? other : other.x),
                       op(this.y, scalar ? other : other.y),
                       size >= 3 ? op(this.z, z) : undefined,
                       size === 4 ? op(this.w, scalar ? other : other.w) : undefined);
      });
    }
    method('dot', function (other) {
      let sum = this.x * other.x + this.y * other.y;
      if (size >= 3) sum += this.z * other.z;
      if (size === 4) sum += this.w * other.w;
      return sum;
    });
    if (size === 3) method('cross', function (other) {
      return new Vec(this.y * other.z - this.z * other.y,
                     this.z * other.x - this.x * other.z,
                     this.x * other.y - this.y * other.x);
    });
    method('mix', function (other, amount) {
      return make(read(this).map((a, i) => a + (other[keys[i]] - a) *
        (typeof amount === 'number' ? amount : amount[keys[i]])));
    });
    for (const [name, op] of Object.entries({abs: Math.abs, sign: Math.sign, round: Math.round,
        floor: Math.floor, ceil: Math.ceil})) {
      method(name, function () { return make(read(this).map(op)); });
    }
    method('toString', function () { return read(this).join(' '); });
    globalThis['Vec' + size] = Vec;
  }
})();
