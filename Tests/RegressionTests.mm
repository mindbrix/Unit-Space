//
//  RegressionTests.mm
//  Unit Space
//
//  Geometry regression tests: counts and values from the CPU pipeline (SVG import, refineChords, triangulation), never
//  pixels. Build and run with Tests/run.sh. Each test runs in its own process under a time limit, so a hang or a crash
//  fails that test alone. Tests/golden.txt holds every corpus file's counts and mesh hash; ./Tests/run.sh --update
//  rewrites it after an intended change. The hashes are of float results, so they hold for one architecture (arm64):
//  fused multiply-adds round differently elsewhere.
//
#import <Foundation/Foundation.h>
#include <spawn.h>
#include <sys/wait.h>
#include <chrono>
#include <map>
#include <string>
#include "Scene.hpp"
#include "libTess.hpp"

extern char **environ;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

static Scene sceneFromSVG(const std::string& body) {
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">" + body + "</svg>";
    Scene scene;
    scene.addSVG(svg.data(), svg.size());
    return scene;
}
static Scene sceneFromFile(const char *path) {
    NSData *data = [NSData dataWithContentsOfFile:@(path)];
    Scene scene;
    if (data)
        scene.addSVG(data.bytes, data.length);
    return scene;
}
static size_t floats(Path& path) {
    size_t n = 0;
    for (SubPath& s : path._subPaths)
        n += s._quadratics.count();
    return n;
}
static size_t floats(Scene& scene) {
    size_t n = 0;
    for (Path& p : scene._paths)
        n += floats(p);
    return n;
}
static uint64_t fnv(const void *bytes, size_t size, uint64_t h = 1469598103934665603ull) {
    for (size_t i = 0; i < size; i++)
        h = (h ^ ((const unsigned char *)bytes)[i]) * 1099511628211ull;
    return h;
}
static uint64_t meshHash(Mesh& mesh) {
    uint64_t h = fnv(mesh._tris.get(), mesh._tris.count() * sizeof(Triangle));
    h = fnv(mesh._interiorVerts.get(), mesh._interiorVerts.count() * sizeof(MeshVertex), h);
    return fnv(mesh._interiorIdxs.get(), mesh._interiorIdxs.count() * sizeof(unsigned int), h);
}
static size_t hulls(Mesh& mesh) {
    size_t n = 0;
    for (size_t i = 0; i < mesh._tris.count(); i++)
        n += (mesh._tris[i].curves & Triangle::kHull) != 0;
    return n;
}
// A mesh vertex's position in path space, from its path's bounds.
static Vector meshPoint(Mesh& mesh, unsigned int p, unsigned short x, unsigned short y) {
    const Bounds& b = mesh._timeline.basePaths()[p & Triangle::kFlagsMask].bounds;
    return Vector(b.lx + x * (b.ux - b.lx) / float(kUnsignedShortMax), b.ly + y * (b.uy - b.ly) / float(kUnsignedShortMax));
}
// Scene space flips SVG's y about the image height.
static bool near(Vector v, float x, float svgY, float height, float r) { return std::hypot(v.x - x, v.y - (height - svgY)) < r; }

#pragma mark - Input limits

// Coordinates beyond coordinateLimit() overflow the pipeline's fourth-power tests: their subpaths are dropped.
static void testHugeCoordinates() {
    Scene scene = sceneFromSVG("<path d=\"M0 0 C1e30 0 0 1e30 1e30 1e30 Z\"/>");
    CHECK(scene._paths.size() == 0);
    Mesh mesh;
    libTess::triangulate(mesh, scene);
}
// A stroke wider than the limit: its offsets and caps lie beyond it and go, leaving only the folds along the curve
// itself. It once crashed the tessellator.
static void testHugeStrokeWidth() {
    Scene scene = sceneFromSVG("<path d=\"M0 0 C10 0 0 10 10 10\" fill=\"none\" stroke=\"black\" stroke-width=\"1e30\"/>");
    float limit = coordinateLimit();
    for (Path& path : scene._paths)
        for (SubPath& s : path._subPaths)
            CHECK(s._bounds.lx >= -limit && s._bounds.ux <= limit && s._bounds.ly >= -limit && s._bounds.uy <= limit);
    Mesh mesh;
    libTess::triangulate(mesh, scene);
    Scene wide = sceneFromSVG("<path d=\"M0 0 C10 0 0 10 10 10\" fill=\"none\" stroke=\"black\" stroke-width=\"1e9\"/>");
    Mesh wideMesh;
    libTess::triangulate(wideMesh, wide);
    CHECK(wideMesh._tris.count() > 0);
}
// Within the limit a shape converts the same at any magnitude, as the tolerance follows its merge limit, and a cubic
// makes at most 43 quadratics.
static void testMagnitudeInvariance() {
    size_t counts[3], triangles[3], k = 0;
    for (const char *e : { "1e8", "1e9", "1.5e9" }) {
        std::string s = e;
        Scene scene = sceneFromSVG("<path d=\"M0 0 C" + s + " 0 " + s + " " + s + " 0 " + s + " Z\"/>");
        CHECK(scene._paths.size() == 1);
        if (scene._paths.size() != 1)
            return;
        Mesh mesh;
        libTess::triangulate(mesh, scene);
        counts[k] = (floats(scene) - 2) / 4, triangles[k++] = mesh._tris.count();
    }
    CHECK(counts[0] == counts[1] && counts[1] == counts[2]);
    CHECK(triangles[0] == triangles[1] && triangles[1] == triangles[2] && triangles[0] > 0);
    CHECK(counts[0] <= 43 + 1);     // the cubic's pieces, and the closing line
    CHECK(sceneFromSVG("<path d=\"M0 0 C1.6e9 0 1.6e9 1.6e9 0 1.6e9 Z\"/>")._paths.size() == 0);
}
// A dash period within the merge limit can't be resolved, so it strokes solid, quickly. It once ran for minutes.
static void testUnresolvableDashes() {
    auto stroke = [](const std::string& dashes) {
        Scene scene = sceneFromSVG("<path d=\"M0 0 L10000 0\" fill=\"none\" stroke=\"black\" stroke-width=\"1\"" + dashes + "/>");
        Mesh mesh;
        libTess::triangulate(mesh, scene);
        return meshHash(mesh);
    };
    CHECK(stroke(" stroke-dasharray=\"0.0001\"") == stroke(""));
}
// Dashes whose arc length outgrows float resolution once stalled the dash loop.
static void testDashProgress() {
    Scene scene = sceneFromSVG("<path d=\"M0 0 L100000 0\" fill=\"none\" stroke=\"black\" stroke-width=\"1\" stroke-dasharray=\"0.004\"/>");
    Mesh mesh;
    libTess::triangulate(mesh, scene);
}
// Zero-length dashes with round caps are dots, which the limit must not mistake for unresolvable dashes.
static void testDots() {
    Scene scene = sceneFromSVG("<path d=\"M0 50 C30 0 70 100 100 50\" fill=\"none\" stroke=\"black\" stroke-width=\"3\" stroke-linecap=\"round\" stroke-dasharray=\"0 4\"/>");
    CHECK(scene._paths.size() == 1);
    CHECK(scene._paths.size() == 1 && scene._paths[0]._subPaths.size() > 10);
}

#pragma mark - Pipeline invariants

// Triangulating leaves the scene as built, so doing it again gives the same mesh.
static void testSceneImmutable() {
    Scene scene = sceneFromFile("Tests/cases/loop-lens.svg");
    CHECK(scene._paths.size() == 1);
    size_t before = floats(scene);
    Mesh first, second;
    libTess::triangulate(first, scene);
    CHECK(floats(scene) == before);
    libTess::triangulate(second, scene);
    CHECK(meshHash(first) == meshHash(second));
}
// Failed allocations throw std::bad_alloc and leave the memory as it was.
static void testMemoryBadAlloc() {
    const size_t huge = SIZE_MAX / 16;
    Memory<float> m;
    m.push_back(1), m.push_back(2);
    bool thrown = false;
    try { m.alloc(huge); } catch (std::bad_alloc&) { thrown = true; }
    CHECK(thrown && m.count() == 2 && m[0] == 1 && m[1] == 2);
    Memory<Triangle, true> paged;
    paged.new_ptr(1)->p = 42;
    thrown = false;
    try { paged.alloc(huge); } catch (std::bad_alloc&) { thrown = true; }
    CHECK(thrown && paged[0].p == 42);
}
// libTess reports a failed allocator by returning no tessellator, which triangulateRange() relies on.
static void testTessellatorAllocationFailure() {
    TESSalloc fail = {};
    fail.memalloc = [](void *, unsigned int) -> void * { return nullptr; };
    fail.memrealloc = [](void *, void *, unsigned int) -> void * { return nullptr; };
    fail.memfree = [](void *, void *) {};
    CHECK(tessNewTess(& fail) == nullptr);
}

#pragma mark - refineChords

// Curves that meet tangentially at smooth joins never overlap, so a circle is left whole. Fused multiply-adds once
// made touching hulls test as overlapping, tripling the splitting.
static void testSmoothJoinsUnsplit() {
    Scene scene = sceneFromSVG("<circle cx=\"50\" cy=\"50\" r=\"40\"/><ellipse cx=\"50\" cy=\"50\" rx=\"40\" ry=\"10\" transform=\"rotate(30 50 50)\"/>");
    CHECK(scene._paths.size() == 2);
    for (Path& path : scene._paths) {
        Path refined = path;
        libTess::refineChords(refined);
        CHECK(floats(refined) == floats(path));
    }
}
// Curves sharing an end whose hulls overlap beyond it: the slit's two edges cross without their chords crossing, so
// they must split.
static void testSlitLobesSplit() {
    Scene scene = sceneFromFile("Tests/cases/slit-lobes.svg");
    CHECK(scene._paths.size() == 1);
    if (scene._paths.size() != 1)
        return;
    Path refined = scene._paths[0];
    libTess::refineChords(refined);
    CHECK(floats(refined) > floats(scene._paths[0]));
}
// A curve crossing a loop's tip twice with no chords crossing: only overlapping hulls reveal it.
static void testLoopLensSplit() {
    Scene scene = sceneFromFile("Tests/cases/loop-lens.svg");
    CHECK(scene._paths.size() == 1);
    if (scene._paths.size() != 1)
        return;
    Path refined = scene._paths[0];
    libTess::refineChords(refined);
    CHECK(floats(refined) > floats(scene._paths[0]));
}

#pragma mark - Triangulation

// A slit's end is a reflex corner with a miter thousands long. Its interior inset slides along each triangle's own
// edge instead, so no interior miter there is that long: the triangle fanning beyond it once kept its, and the
// interior culled it below 381x.
static void testReflexInset() {
    Scene scene = sceneFromFile("Tests/cases/slit-reflex-inset.svg");
    Mesh mesh;
    libTess::triangulate(mesh, scene);
    int corners = 0;
    for (size_t i = 0; i < mesh._tris.count(); i++) {
        Triangle& t = mesh._tris[i];
        if (t.curves & Triangle::kHull)
            continue;
        unsigned short xs[3] = { t.x0, t.x1, t.x2 }, ys[3] = { t.y0, t.y1, t.y2 };
        for (int k = 0; k < 3; k++)
            if (near(meshPoint(mesh, t.p, xs[k], ys[k]), 212.64f, 127.493f, 510, 1e-3f)) {
                const float *m = & mesh._interiorMiters[i * 6 + k * 2];
                CHECK(std::hypot(m[0], m[1]) < kTriangleDilationMax);
                corners++;
            }
    }
    CHECK(corners > 0);
}
// A slit whose two curves leave its end along the same tangent: the join turns back on itself, so it is reflex, not
// smooth. Classed convex, the hulls were cut away to their chords.
static void testReversalJoin() {
    Scene scene = sceneFromFile("Tests/cases/slit-reversal-join.svg");
    Mesh mesh;
    libTess::triangulate(mesh, scene);
    int ends = 0;
    for (size_t i = 0; i < mesh._tris.count(); i++) {
        Triangle& t = mesh._tris[i];
        if (!(t.curves & Triangle::kHull))
            continue;
        if (near(meshPoint(mesh, t.p, t.x0, t.y0), 212.64f, 127.493f, 510, 1e-3f)) {
            CHECK(!(t.p & Triangle::kFlagsConvex0));
            ends++;
        }
        if (near(meshPoint(mesh, t.p, t.x2, t.y2), 212.64f, 127.493f, 510, 1e-3f)) {
            CHECK(!(t.p & Triangle::kFlagsConvex2));
            ends++;
        }
    }
    CHECK(ends == 2);
}

#pragma mark - Corpus

// A file's scene floats, then its mesh's triangles, hulls and hash.
static std::string summary(const char *path) {
    Scene scene = sceneFromFile(path);
    Mesh mesh;
    libTess::triangulate(mesh, scene);
    char line[256];
    snprintf(line, sizeof(line), "%zu %zu %zu %016llx", floats(scene), mesh._tris.count(), hulls(mesh), (unsigned long long)meshHash(mesh));
    return line;
}
static std::map<std::string, std::string> readGolden() {
    std::map<std::string, std::string> golden;
    NSString *text = [NSString stringWithContentsOfFile:@"Tests/golden.txt" encoding:NSUTF8StringEncoding error:nil];
    for (NSString *line in [text componentsSeparatedByString:@"\n"]) {
        NSRange tab = [line rangeOfString:@"\t"];
        if (line.length && ![line hasPrefix:@"#"] && tab.location != NSNotFound)
            golden[[line substringToIndex:tab.location].UTF8String] = [line substringFromIndex:tab.location + 1].UTF8String;
    }
    return golden;
}
static std::vector<std::string> corpus() {
    std::vector<std::string> files;
    for (NSString *dir in @[ @"SVG", @"Tests/cases" ])
        for (NSString *name in [[[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil] sortedArrayUsingSelector:@selector(compare:)])
            if ([name.pathExtension isEqualToString:@"svg"])
                files.push_back([dir stringByAppendingPathComponent:name].UTF8String);
    return files;
}

#pragma mark - Runner

struct Test { const char *name; void (*run)(); int seconds; };
static const Test kTests[] = {
    { "limits: huge coordinates", testHugeCoordinates, 10 },
    { "limits: huge stroke width", testHugeStrokeWidth, 10 },
    { "limits: magnitude invariance", testMagnitudeInvariance, 10 },
    { "limits: unresolvable dashes", testUnresolvableDashes, 10 },
    { "limits: dash progress", testDashProgress, 10 },
    { "limits: dots", testDots, 10 },
    { "pipeline: scene immutable", testSceneImmutable, 10 },
    { "pipeline: Memory bad_alloc", testMemoryBadAlloc, 10 },
    { "pipeline: tessellator allocation failure", testTessellatorAllocationFailure, 10 },
    { "refineChords: smooth joins unsplit", testSmoothJoinsUnsplit, 10 },
    { "refineChords: slit lobes split", testSlitLobesSplit, 10 },
    { "refineChords: loop lens split", testLoopLensSplit, 10 },
    { "triangulate: reflex inset", testReflexInset, 10 },
    { "triangulate: reversal join", testReversalJoin, 10 },
};

// Runs this program again as `--run <test>` or `--corpus <file>`, killed after seconds. Returns its output, and its
// exit status (or a description of how it died) in result.
static std::string spawn(const char *self, const char *mode, const char *arg, int seconds, std::string& result) {
    int fds[2];
    pipe(fds);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(& actions);
    posix_spawn_file_actions_adddup2(& actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(& actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(& actions, fds[0]);
    char *argv[] = { (char *)self, (char *)mode, (char *)arg, nullptr };
    pid_t pid;
    if (posix_spawn(& pid, self, & actions, nullptr, argv, environ) != 0) {
        result = "could not spawn";
        return "";
    }
    posix_spawn_file_actions_destroy(& actions);
    close(fds[1]);
    // Read on another thread, so a child filling the pipe can't block.
    __block std::string output;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    int fd = fds[0];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
        char buffer[4096];
        ssize_t n;
        while ((n = read(fd, buffer, sizeof(buffer))) > 0)
            output.append(buffer, n);
        dispatch_semaphore_signal(done);
    });
    auto start = std::chrono::steady_clock::now();
    int status = 0;
    bool timedOut = false;
    while (waitpid(pid, & status, WNOHANG) == 0) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(seconds)) {
            kill(pid, SIGKILL), waitpid(pid, & status, 0), timedOut = true;
            break;
        }
        usleep(1000);
    }
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    close(fd);
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    char text[64];
    if (timedOut)
        snprintf(text, sizeof(text), "timed out after %ds", seconds);
    else if (WIFSIGNALED(status))
        snprintf(text, sizeof(text), "crashed (signal %d)", WTERMSIG(status));
    else
        snprintf(text, sizeof(text), WEXITSTATUS(status) ? "failed (%.2fs)" : "ok (%.2fs)", elapsed);
    result = text;
    return output;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc == 3 && strcmp(argv[1], "--run") == 0) {
            for (const Test& t : kTests)
                if (strcmp(t.name, argv[2]) == 0)
                    t.run();
            return failures ? 1 : 0;
        }
        if (argc == 3 && strcmp(argv[1], "--corpus") == 0) {
            printf("SUMMARY %s\n", summary(argv[2]).c_str());
            return 0;
        }
        bool update = argc == 2 && strcmp(argv[1], "--update") == 0;
        int failed = 0;
        std::string result;
        if (!update)
            for (const Test& t : kTests) {
                std::string output = spawn(argv[0], "--run", t.name, t.seconds, result);
                bool ok = result.compare(0, 2, "ok") == 0;
                printf("%-44s %s\n", t.name, result.c_str());
                if (!ok) {
                    failed++;
                    // The test's own lines, without nanosvg's parse messages.
                    NSArray *lines = [@(output.c_str()) componentsSeparatedByString:@"\n"];
                    for (NSString *line in lines)
                        if ([line hasPrefix:@"    "])
                            printf("%s\n", line.UTF8String);
                }
            }
        std::map<std::string, std::string> golden = readGolden();
        NSMutableString *updated = [NSMutableString stringWithString:@"# Corpus regression baseline: file, scene floats, triangles, hulls, mesh hash (arm64). Rewrite with ./Tests/run.sh --update.\n"];
        for (const std::string& file : corpus()) {
            std::string output = spawn(argv[0], "--corpus", file.c_str(), 120, result);
            size_t at = output.find("SUMMARY ");
            std::string line = at == std::string::npos ? "" : output.substr(at + 8, output.find('\n', at) - at - 8);
            if (update) {
                printf("%-44s %s\n", file.c_str(), line.empty() ? result.c_str() : line.c_str());
                [updated appendFormat:@"%@\t%@\n", @(file.c_str()), @(line.c_str())];
                continue;
            }
            auto expected = golden.find(file);
            bool ok = !line.empty() && expected != golden.end() && expected->second == line;
            printf("%-44s %s\n", ("corpus: " + file).c_str(), ok ? result.c_str() : line.empty() ? result.c_str() : "changed");
            if (!ok) {
                failed++;
                printf("    expected %s\n    actual   %s\n", expected == golden.end() ? "(not in golden.txt)" : expected->second.c_str(), line.c_str());
            }
        }
        if (update) {
            [updated writeToFile:@"Tests/golden.txt" atomically:YES encoding:NSUTF8StringEncoding error:nil];
            printf("Wrote Tests/golden.txt\n");
            return 0;
        }
        printf(failed ? "\n%d FAILED\n" : "\nAll passed\n", failed);
        return failed ? 1 : 0;
    }
}
