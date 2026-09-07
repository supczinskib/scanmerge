// SPDX-License-Identifier: GPL-3.0-only

import Foundation

struct Failure: Error { let message: String }
func check(_ value: @autoclosure () throws -> Bool, _ message: String) throws {
    if try !value() { throw Failure(message: message) }
}
@main struct Tests {
    static func main() throws {
        let fm = FileManager.default
        let base = fm.temporaryDirectory.appendingPathComponent(
            "scanmerge-terminal-test-" + UUID().uuidString)
        try fm.createDirectory(at: base, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: base) }
        let home = base.appendingPathComponent("home with spaces")
        try fm.createDirectory(at: home, withIntermediateDirectories: true)
        let app = base.appendingPathComponent(
            "Applications with spaces/ScanMerge.app/Contents/MacOS/scanmerge")
        try fm.createDirectory(
            at: app.deletingLastPathComponent(), withIntermediateDirectories: true)
        try "#!/bin/sh\nprintf 'works\\n'\n".write(to: app, atomically: true, encoding: .utf8)
        try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: app.path)
        let profile = home.appendingPathComponent(".zshrc")
        let original = "# user settings\nexport MY_SETTING='retain me'"  // no trailing newline
        try original.write(to: profile, atomically: true, encoding: .utf8)
        try fm.setAttributes([.posixPermissions: 0o600], ofItemAtPath: profile.path)
        let t = TerminalIntegration(home: home, executable: app, shell: "/bin/zsh", zDotDir: nil)
        try t.enable()
        try check(t.isEnabled, "link enabled")
        let first = try String(contentsOf: profile, encoding: .utf8)
        try check(first.hasPrefix(original), "preserve user configuration")
        try t.enable()
        let second = try String(contentsOf: profile, encoding: .utf8)
        try check(first == second, "idempotent enable")
        try check(
            (try fm.attributesOfItem(atPath: profile.path)[.posixPermissions] as? Int) == 0o600,
            "preserve permissions")
        let p = Process()
        p.executableURL = t.link
        let pipe = Pipe()
        p.standardOutput = pipe
        try p.run()
        p.waitUntilExit()
        try check(p.terminationStatus == 0, "link executes with spaces")
        let shellCheck = Process()
        shellCheck.executableURL = URL(fileURLWithPath: "/bin/zsh")
        shellCheck.arguments = ["-ic", "command -v scanmerge"]
        shellCheck.environment = ["HOME": home.path, "ZDOTDIR": home.path, "PATH": "/usr/bin:/bin"]
        let shellOutput = Pipe()
        shellCheck.standardOutput = shellOutput
        try shellCheck.run()
        shellCheck.waitUntilExit()
        let discovered = String(
            decoding: shellOutput.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self
        ).trimmingCharacters(in: .whitespacesAndNewlines)
        try check(
            discovered == t.link.path, "new interactive zsh finds command via configured PATH")
        let moved = base.appendingPathComponent("Moved.app/Contents/MacOS/scanmerge")
        try fm.createDirectory(
            at: moved.deletingLastPathComponent(), withIntermediateDirectories: true)
        try fm.copyItem(at: app, to: moved)
        let updated = TerminalIntegration(
            home: home, executable: moved, shell: "/bin/zsh", zDotDir: nil)
        try updated.enable()
        try check(
            (try fm.destinationOfSymbolicLink(atPath: t.link.path)) == moved.path,
            "retarget after move")
        try updated.disable()
        try check(
            (try String(contentsOf: profile, encoding: .utf8)) == original,
            "restore exact original bytes")
        try check(!fm.fileExists(atPath: t.link.path), "remove managed link")
        // Do not clobber an unrelated command.
        try "foreign command".write(to: t.link, atomically: true, encoding: .utf8)
        var rejected = false
        do { try t.enable() } catch { rejected = true }
        try check(rejected, "foreign command rejected")
        try check(
            (try String(contentsOf: t.link, encoding: .utf8)) == "foreign command",
            "foreign command unchanged")
        try fm.removeItem(at: t.link)
        try t.enable()
        try (String(contentsOf: profile, encoding: .utf8) + "\n# user added this later\n").write(
            to: profile, atomically: true, encoding: .utf8)
        try t.disable()
        try check(
            (try String(contentsOf: profile, encoding: .utf8)).contains("# user added this later"),
            "preserve later edits")
        // Config symlinks are not silently replaced.
        try fm.removeItem(at: profile)
        let external = base.appendingPathComponent("external-zshrc")
        try original.write(to: external, atomically: true, encoding: .utf8)
        try fm.createSymbolicLink(at: profile, withDestinationURL: external)
        rejected = false
        do { try t.enable() } catch { rejected = true }
        try check(rejected, "reject linked configuration")
        try check(
            (try String(contentsOf: external, encoding: .utf8)) == original,
            "linked config untouched")
        // An initially missing profile is removed on uninstall only while empty.
        try fm.removeItem(at: profile)
        try t.enable()
        try t.disable()
        try check(
            !fm.fileExists(atPath: profile.path), "clean uninstall of initially missing profile")
        print(
            "PASS: temporary home only; enable/disable; idempotence; moved app; spaces; permissions; foreign command; user edits; symlink config; missing profile"
        )
    }
}
