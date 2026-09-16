#ifdef _WIN32
    #define _CRT_SECURE_NO_WARNINGS
    #include <malloc.h>
    #define aligned_alloc(a, s) _aligned_malloc(s, a)
    #define free_aligned(p) _aligned_free(p)
#else
    #define _POSIX_C_SOURCE 200809L
    #define free_aligned(p) free(p)
#endif

#include <GLFW/glfw3.h>

#ifdef _WIN32
    #include <GL/gl.h>
#else
    #include <GL/gl.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <omp.h>

#include "flower_vert.h"
#include "flower_frag.h"

#define T_COUNT 1151
#define X_COUNT 25
#define VERTEX_COUNT (T_COUNT * X_COUNT)
#define INDEX_COUNT ((X_COUNT - 1) * (T_COUNT - 1) * 6)
#define PI 3.14159265358979323846f

typedef struct { float x, y, z, value; } Vertex;
typedef struct { float x, y, z, r, g, b; } GridVertex;
typedef struct { float w, x, y, z; } Quat;

static GLFWwindow *g_win;
static GLuint g_prog, g_grid_prog, g_vao, g_grid_vao, g_vbo, g_ebo, g_grid_vbo;
static float g_dist = 4.0f;
static Quat g_rot = {1, 0, 0, 0};
static bool g_drag = false;
static double g_lx, g_ly;
static int g_w = 1280, g_h = 720;

static void fatal(const char *m) { fprintf(stderr, "ERROR: %s\n", m); glfwTerminate(); exit(1); }

static GLuint compile_shader(GLenum t, const char *s) {
    GLuint sh = glCreateShader(t);
    glShaderSource(sh, 1, &s, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        char *log = malloc(len + 1);
        glGetShaderInfoLog(sh, len, NULL, log);
        fprintf(stderr, "Shader error:\n%s\n", log);
        free(log);
        exit(1);
    }
    return sh;
}

static GLuint create_program(const char *vs_src, const char *fs_src) {
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        char *log = malloc(len + 1);
        glGetProgramInfoLog(p, len, NULL, log);
        fprintf(stderr, "Link error:\n%s\n", log);
        free(log);
        exit(1);
    }
    return p;
}

static void mat4_mul(float *o, const float *a, const float *b) {
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r[c * 4 + row] = a[row] * b[c * 4] + a[4 + row] * b[c * 4 + 1] +
                            a[8 + row] * b[c * 4 + 2] + a[12 + row] * b[c * 4 + 3];
    memcpy(o, r, sizeof(r));
}

static void mat4_persp(float *m, float fovy, float asp, float zn, float zf) {
    float f = 1.0f / tanf(fovy * PI / 360.0f);
    memset(m, 0, 64);
    m[0] = f / asp; m[5] = f; m[10] = (zf + zn) / (zn - zf);
    m[11] = -1; m[14] = (2 * zf * zn) / (zn - zf);
}

static void mat4_trans(float *m, float x, float y, float z) {
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1;
    m[12] = x; m[13] = y; m[14] = z;
}

static Quat quat_norm(Quat q) {
    float l = sqrtf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (l < 1e-12f) return (Quat){1,0,0,0};
    float i = 1.0f/l;
    return (Quat){q.w*i, q.x*i, q.y*i, q.z*i};
}

static Quat quat_mul(Quat a, Quat b) {
    return quat_norm((Quat){
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w
    });
}

static Quat quat_aa(float ax, float ay, float az, float a) {
    float h = a * 0.5f, s = sinf(h);
    return (Quat){cosf(h), ax*s, ay*s, az*s};
}

static void quat_mat4(Quat q, float *m) {
    q = quat_norm(q);
    float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
    float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
    float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
    m[0]=1-2*(yy+zz); m[1]=2*(xy+wz); m[2]=2*(xz-wy); m[3]=0;
    m[4]=2*(xy-wz); m[5]=1-2*(xx+zz); m[6]=2*(yz+wx); m[7]=0;
    m[8]=2*(xz+wy); m[9]=2*(yz-wx); m[10]=1-2*(xx+yy); m[11]=0;
    m[12]=m[13]=m[14]=0; m[15]=1;
}

static void build_mvp(float *out, int with_rot) {
    float proj[16], rot[16], trans[16], vm[16];
    float asp = g_h > 0 ? (float)g_w / g_h : 1.0f;
    mat4_persp(proj, 45.0f, asp, 0.01f, 100.0f);
    mat4_trans(trans, 0, 0, -g_dist);
    if (with_rot) {
        quat_mat4(g_rot, rot);
        mat4_mul(vm, trans, rot);
    } else {
        memcpy(vm, trans, sizeof(vm));
    }
    mat4_mul(out, proj, vm);
}

static void apply_rot(float dx, float dy) {
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1e-9f) return;
    Quat d = quat_aa(dy/len, dx/len, 0, len * 0.008f);
    g_rot = quat_mul(d, g_rot);
}

static Vertex *gen_flower(void) {
    Vertex *v = aligned_alloc(64, sizeof(Vertex) * VERTEX_COUNT);
    if (!v) fatal("Alloc failed");
    float *t = aligned_alloc(64, sizeof(float) * T_COUNT);
    float *p = aligned_alloc(64, sizeof(float) * T_COUNT);
    float *u = aligned_alloc(64, sizeof(float) * T_COUNT);
    if (!t || !p || !u) fatal("Alloc failed");

    #pragma omp parallel for schedule(static)
    for (int j = 0; j < T_COUNT; j++) {
        float raw = 0.5f * j;
        t[j] = raw / 575.0f * 17.0f * PI - 2.0f * PI;
        p[j] = PI / 2.0f * expf(-t[j] / (8.0f * PI));
        float mod = fmodf(3.6f * t[j], 2.0f * PI);
        if (mod < 0) mod += 2.0f * PI;
        u[j] = 1.0f - powf(1.0f - mod / PI, 4.0f) / 2.0f;
    }

    #pragma omp parallel for schedule(static)
    for (int idx = 0; idx < VERTEX_COUNT; idx++) {
        int i = idx / T_COUNT, j = idx % T_COUNT;
        float x = (float)i / 24.0f;
        float y = 2.0f * powf(x * x - x, 2.0f) * sinf(p[j]);
        float r = u[j] * (x * sinf(p[j]) + y * cosf(p[j]));
        v[idx].x = r * cosf(t[j]);
        v[idx].y = r * sinf(t[j]);
        v[idx].z = u[j] * (x * cosf(p[j]) - y * sinf(p[j]));
        v[idx].value = v[idx].z;
    }

    float minz = INFINITY, maxz = -INFINITY;
    #pragma omp parallel
    {
        float lmin = INFINITY, lmax = -INFINITY;
        #pragma omp for nowait
        for (int i = 0; i < VERTEX_COUNT; i++) {
            if (v[i].value < lmin) lmin = v[i].value;
            if (v[i].value > lmax) lmax = v[i].value;
        }
        #pragma omp critical
        { if (lmin < minz) minz = lmin; if (lmax > maxz) maxz = lmax; }
    }
    float range = maxz - minz;
    if (range < 1e-8f) range = 1.0f;
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < VERTEX_COUNT; i++)
        v[i].value = (v[i].value - minz) / range;

    free_aligned(t); free_aligned(p); free_aligned(u);
    return v;
}

static GLuint *gen_indices(void) {
    GLuint *idx = aligned_alloc(64, sizeof(GLuint) * INDEX_COUNT);
    if (!idx) fatal("Alloc failed");
    int k = 0;
    for (int i = 0; i < X_COUNT - 1; i++)
        for (int j = 0; j < T_COUNT - 1; j++) {
            GLuint a = i * T_COUNT + j, b = a + 1;
            GLuint c = (i + 1) * T_COUNT + j, d = c + 1;
            idx[k++] = a; idx[k++] = b; idx[k++] = c;
            idx[k++] = b; idx[k++] = d; idx[k++] = c;
        }
    return idx;
}

static GLuint create_grid_prog(void) {
    const char *vs = "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec3 aColor;\n"
        "uniform mat4 uMVP;\n"
        "out vec3 vColor;\n"
        "void main(){gl_Position=uMVP*vec4(aPos,1.0);vColor=aColor;}\n";
    const char *fs = "#version 330 core\n"
        "in vec3 vColor;\n"
        "out vec4 FragColor;\n"
        "void main(){FragColor=vec4(vColor,1.0);}\n";
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) fatal("Grid shader link failed");
    return p;
}

static void create_grid(void) {
    const float R = 1.5f, step = 0.25f;
    const int div = (int)roundf(2.0f * R / step);
    const int nv = 3 * (div + 1) * 4 + 6;
    GridVertex *v = aligned_alloc(64, sizeof(GridVertex) * nv);
    if (!v) fatal("Alloc failed");
    int n = 0;
    const float gr = 0.25f;

    for (int k = 0; k <= div; k++) {
        float q = -R + k * step;
        v[n++] = (GridVertex){-R, q, -R, gr, gr, gr};
        v[n++] = (GridVertex){ R, q, -R, gr, gr, gr};
        v[n++] = (GridVertex){q, -R, -R, gr, gr, gr};
        v[n++] = (GridVertex){q,  R, -R, gr, gr, gr};
    }
    for (int k = 0; k <= div; k++) {
        float q = -R + k * step;
        v[n++] = (GridVertex){-R, -R, q, gr, gr, gr};
        v[n++] = (GridVertex){ R, -R, q, gr, gr, gr};
        v[n++] = (GridVertex){q, -R, -R, gr, gr, gr};
        v[n++] = (GridVertex){q, -R,  R, gr, gr, gr};
    }
    for (int k = 0; k <= div; k++) {
        float q = -R + k * step;
        v[n++] = (GridVertex){-R, -R, q, gr, gr, gr};
        v[n++] = (GridVertex){-R,  R, q, gr, gr, gr};
        v[n++] = (GridVertex){-R, q, -R, gr, gr, gr};
        v[n++] = (GridVertex){-R, q,  R, gr, gr, gr};
    }

    v[n++] = (GridVertex){-R, -R, -R, 1, 0, 0};
    v[n++] = (GridVertex){ R, -R, -R, 1, 0, 0};
    v[n++] = (GridVertex){-R, -R, -R, 0, 1, 0};
    v[n++] = (GridVertex){-R,  R, -R, 0, 1, 0};
    v[n++] = (GridVertex){-R, -R, -R, 0, 0, 1};
    v[n++] = (GridVertex){-R, -R,  R, 0, 0, 1};

    glGenVertexArrays(1, &g_grid_vao);
    glGenBuffers(1, &g_grid_vbo);
    glBindVertexArray(g_grid_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_grid_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GridVertex) * n, v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GridVertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GridVertex), (void*)offsetof(GridVertex, r));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    free_aligned(v);
}

static void render_grid(const float *mvp) {
    glUseProgram(g_grid_prog);
    GLint loc = glGetUniformLocation(g_grid_prog, "uMVP");
    glUniformMatrix4fv(loc, 1, GL_FALSE, mvp);
    glBindVertexArray(g_grid_vao);
    glDrawArrays(GL_LINES, 0, 3 * ((int)roundf(3.0f / 0.25f) + 1) * 4 + 6);
    glBindVertexArray(0);
}

static void render_ticks(const float *mvp) {
    const float R = 1.5f, step = 0.25f;
    const int div = (int)roundf(2.0f * R / step);
    const int nv = (div + 1) * 6;
    GridVertex *t = aligned_alloc(64, sizeof(GridVertex) * nv);
    if (!t) return;
    int n = 0;
    const float ts = 0.05f;
    for (int k = 0; k <= div; k++) {
        float q = -R + k * step;
        t[n++] = (GridVertex){q, -R, -R-ts, 1, 0, 0};
        t[n++] = (GridVertex){q, -R, -R+ts, 1, 0, 0};
        t[n++] = (GridVertex){-R, q, -R-ts, 0, 1, 0};
        t[n++] = (GridVertex){-R, q, -R+ts, 0, 1, 0};
        t[n++] = (GridVertex){-R-ts, -R, q, 0, 0, 1};
        t[n++] = (GridVertex){-R+ts, -R, q, 0, 0, 1};
    }
    glUseProgram(g_grid_prog);
    GLint loc = glGetUniformLocation(g_grid_prog, "uMVP");
    glUniformMatrix4fv(loc, 1, GL_FALSE, mvp);
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GridVertex) * n, t, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GridVertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GridVertex), (void*)offsetof(GridVertex, r));
    glEnableVertexAttribArray(1);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, n);
    glLineWidth(1.0f);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    free_aligned(t);
}

static void upload_mesh(void) {
    double start = glfwGetTime();
    Vertex *v = gen_flower();
    GLuint *idx = gen_indices();
    printf("Flower generated in %.3f ms using %d threads\n",
           (glfwGetTime() - start) * 1000.0, omp_get_max_threads());

    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glGenBuffers(1, &g_ebo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * VERTEX_COUNT, v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint) * INDEX_COUNT, idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, value));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    free_aligned(v); free_aligned(idx);
}

static void mouse_cb(GLFWwindow *w, int btn, int act, int mods) {
    (void)w; (void)mods;
    if (btn != GLFW_MOUSE_BUTTON_LEFT) return;
    if (act == GLFW_PRESS) {
        g_drag = true;
        glfwGetCursorPos(g_win, &g_lx, &g_ly);
    } else if (act == GLFW_RELEASE) {
        g_drag = false;
    }
}

static void cursor_cb(GLFWwindow *w, double xp, double yp) {
    (void)w;
    if (!g_drag) return;
    float dx = xp - g_lx, dy = yp - g_ly;
    g_lx = xp; g_ly = yp;
    apply_rot(dx, dy);
}

static void scroll_cb(GLFWwindow *w, double xo, double yo) {
    (void)w; (void)xo;
    g_dist -= yo * 0.25f;
    if (g_dist < 1.0f) g_dist = 1.0f;
    if (g_dist > 20.0f) g_dist = 20.0f;
}

static void resize_cb(GLFWwindow *w, int width, int height) {
    (void)w;
    g_w = width; g_h = height;
    glViewport(0, 0, width, height);
}

static void render(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    float mvp[16], gmvp[16];
    build_mvp(mvp, 1);
    build_mvp(gmvp, 0);
    render_grid(gmvp);
    render_ticks(gmvp);

    glUseProgram(g_prog);
    GLint loc = glGetUniformLocation(g_prog, "uMVP");
    glUniformMatrix4fv(loc, 1, GL_FALSE, mvp);
    glBindVertexArray(g_vao);
    glDrawElements(GL_TRIANGLES, INDEX_COUNT, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

int main(void) {
    omp_set_num_threads(omp_get_num_procs());
    if (!glfwInit()) fatal("GLFW init failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    g_win = glfwCreateWindow(g_w, g_h, "Flower - C + OpenGL", NULL, NULL);
    if (!g_win) fatal("Cannot create window");
    glfwMakeContextCurrent(g_win);
    glfwSwapInterval(1);

    glfwSetMouseButtonCallback(g_win, mouse_cb);
    glfwSetCursorPosCallback(g_win, cursor_cb);
    glfwSetScrollCallback(g_win, scroll_cb);
    glfwSetFramebufferSizeCallback(g_win, resize_cb);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glClearColor(0, 0, 0, 1);

    printf("OpenGL: %s %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    g_prog = create_program(g_flower_vert_src, g_flower_frag_src);
    g_grid_prog = create_grid_prog();
    upload_mesh();
    create_grid();

    while (!glfwWindowShouldClose(g_win)) {
        render();
        glfwSwapBuffers(g_win);
        glfwPollEvents();
    }

    glDeleteBuffers(1, &g_ebo);
    glDeleteBuffers(1, &g_vbo);
    glDeleteVertexArrays(1, &g_vao);
    glDeleteBuffers(1, &g_grid_vbo);
    glDeleteVertexArrays(1, &g_grid_vao);
    glDeleteProgram(g_prog);
    glDeleteProgram(g_grid_prog);
    glfwDestroyWindow(g_win);
    glfwTerminate();
    return 0;
}