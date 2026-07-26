#pragma once
#include <cmath>

// Minimal column-major mat4/vec3. Column-major means m[c*4 + r] is row r of
// column c — the layout glUniformMatrix4fv expects with transpose = GL_FALSE.

struct vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

inline vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline vec3 normalize(vec3 v) {
    float len = std::sqrt(dot(v, v));
    return {v.x / len, v.y / len, v.z / len};
}

struct mat4 {
    float m[16] = {};  // column-major: m[col*4 + row]

    static mat4 identity() {
        mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
        return r;
    }

    mat4 operator*(const mat4& b) const {
        mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float s = 0.f;
                for (int k = 0; k < 4; ++k)
                    s += m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
};

inline mat4 translate(vec3 t) {
    mat4 r = mat4::identity();
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
}

inline mat4 rotate(vec3 axis, float angle) {
    vec3 a = normalize(axis);
    float c = std::cos(angle), s = std::sin(angle), t = 1.f - c;
    mat4 r = mat4::identity();
    r.m[0] = c + a.x * a.x * t;
    r.m[1] = a.y * a.x * t + a.z * s;
    r.m[2] = a.z * a.x * t - a.y * s;
    r.m[4] = a.x * a.y * t - a.z * s;
    r.m[5] = c + a.y * a.y * t;
    r.m[6] = a.z * a.y * t + a.x * s;
    r.m[8] = a.x * a.z * t + a.y * s;
    r.m[9] = a.y * a.z * t - a.x * s;
    r.m[10] = c + a.z * a.z * t;
    return r;
}

inline mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    float f = 1.f / std::tan(fovY / 2.f);
    mat4 r;  // all zeros — perspective is not identity-based
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.f;
    r.m[14] = 2.f * zFar * zNear / (zNear - zFar);
    return r;
}

inline mat4 lookAt(vec3 eye, vec3 center, vec3 up) {
    vec3 fwd = normalize(center - eye);
    vec3 side = normalize(cross(fwd, up));
    vec3 u = cross(side, fwd);
    mat4 r = mat4::identity();
    r.m[0] = side.x; r.m[4] = side.y; r.m[8]  = side.z;
    r.m[1] = u.x;    r.m[5] = u.y;    r.m[9]  = u.z;
    r.m[2] = -fwd.x; r.m[6] = -fwd.y; r.m[10] = -fwd.z;
    r.m[12] = -dot(side, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(fwd, eye);
    return r;
}
