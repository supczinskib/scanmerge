// SPDX-License-Identifier: GPL-3.0-only

import Foundation

struct TerminalIntegration {
    struct Receipt: Codable {
        var target: String
        var profile: String
        var insertion: String
        var profileWasMissing: Bool
    }
    struct Issue: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }
    let home: URL
    let executable: URL
    let shell: String
    let zDotDir: URL?
    private let fm = FileManager.default
    var bin: URL { home.appendingPathComponent(".local/bin", isDirectory: true) }
    var link: URL { bin.appendingPathComponent("scanmerge") }
    var receiptURL: URL { home.appendingPathComponent(".local/share/ScanMerge/terminal.json") }
    private var profile: URL {
        shell.hasSuffix("/bash")
            ? home.appendingPathComponent(".bash_profile")
            : (zDotDir ?? home).appendingPathComponent(".zshrc")
    }
    static let block = """
        # >>> ScanMerge PATH >>>
        case ":$PATH:" in
          *":$HOME/.local/bin:"*) ;;
          *) export PATH="$HOME/.local/bin:$PATH" ;;
        esac
        # <<< ScanMerge PATH <<<
        """
    private func exists(_ url: URL) -> Bool {
        (try? fm.attributesOfItem(atPath: url.path)) != nil
    }
    private func target() -> String? { try? fm.destinationOfSymbolicLink(atPath: link.path) }
    private func receipt() throws -> Receipt? {
        guard exists(receiptURL) else { return nil }
        return try JSONDecoder().decode(Receipt.self, from: Data(contentsOf: receiptURL))
    }
    var isManaged: Bool { (try? receipt()) != nil }
    var isEnabled: Bool {
        guard let r = try? receipt() else { return false }
        return target() == r.target && fm.isExecutableFile(atPath: r.target)
    }
    var description: String {
        L10n.text(
            "Link: %@\nConfiguration: %@\nProgram: %@", link.path, profile.path, executable.path)
    }
    private func readProfile(_ url: URL) throws -> String {
        if !exists(url) { return "" }
        let type = try fm.attributesOfItem(atPath: url.path)[.type] as? FileAttributeType
        guard type == .typeRegular else {
            throw Issue(
                message: L10n.text(
                    "The path %@ is not a regular file and was not changed. Use the full path to the application command.",
                    url.path))
        }
        return try String(contentsOf: url, encoding: .utf8)
    }
    private func writeProfile(_ text: String, to url: URL) throws {
        let permissions = (try? fm.attributesOfItem(atPath: url.path)[.posixPermissions]) ?? 0o644
        try text.write(to: url, atomically: true, encoding: .utf8)
        try fm.setAttributes([.posixPermissions: permissions], ofItemAtPath: url.path)
    }
    private func checkShell() throws {
        guard shell.hasSuffix("/zsh") || shell.hasSuffix("/bash") else {
            throw Issue(
                message: L10n.text(
                    "Automatic setup supports zsh and bash. Use the full command path with this shell."
                ))
        }
    }
    @discardableResult func enable() throws -> String {
        try checkShell()
        guard fm.isExecutableFile(atPath: executable.path) else {
            throw Issue(
                message: L10n.text("The application bundle is missing the scanmerge engine."))
        }
        var saved = try receipt()
        let oldTarget = target()
        if exists(link) {
            guard let r = saved, oldTarget == r.target else {
                throw Issue(
                    message: L10n.text(
                        "Another file or link already exists at %@. It will not be overwritten.",
                        link.path))
            }
        }
        let config = saved.map { URL(fileURLWithPath: $0.profile) } ?? profile
        let oldText = try readProfile(config)
        let wasMissing = !exists(config)
        var insertion = saved?.insertion ?? ""
        var newText = oldText
        if let r = saved, !r.insertion.isEmpty {
            guard oldText.contains(r.insertion) else {
                throw Issue(
                    message: L10n.text(
                        "The ScanMerge profile entry was edited manually. The configuration and link were not changed."
                    ))
            }
        } else {
            guard !oldText.contains("# >>> ScanMerge PATH >>>") else {
                throw Issue(
                    message: L10n.text(
                        "The profile contains a ScanMerge entry without an installation receipt. The entry was not changed."
                    ))
            }
            insertion =
                (oldText.isEmpty ? "" : (oldText.hasSuffix("\n") ? "\n" : "\n\n")) + Self.block
                + "\n"
            newText += insertion
        }
        try fm.createDirectory(at: bin, withIntermediateDirectories: true)
        try fm.createDirectory(
            at: receiptURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        let previousReceipt = try? Data(contentsOf: receiptURL)
        var wroteProfile = false
        var changedLink = false
        do {
            if newText != oldText {
                try writeProfile(newText, to: config)
                wroteProfile = true
            }
            if oldTarget != executable.path {
                if exists(link) { try fm.removeItem(at: link) }
                changedLink = true
                try fm.createSymbolicLink(at: link, withDestinationURL: executable)
            }
            saved = Receipt(
                target: executable.path, profile: config.path, insertion: insertion,
                profileWasMissing: saved?.profileWasMissing ?? wasMissing)
            try JSONEncoder().encode(saved!).write(to: receiptURL, options: .atomic)
        } catch {
            if changedLink {
                if target() == executable.path { try? fm.removeItem(at: link) }
                if let oldTarget, !exists(link) {
                    try? fm.createSymbolicLink(atPath: link.path, withDestinationPath: oldTarget)
                }
            }
            if wroteProfile, (try? readProfile(config)) == newText {
                if wasMissing {
                    try? fm.removeItem(at: config)
                } else {
                    try? writeProfile(oldText, to: config)
                }
            }
            if let previousReceipt {
                try? previousReceipt.write(to: receiptURL, options: .atomic)
            } else {
                try? fm.removeItem(at: receiptURL)
            }
            throw error
        }
        return L10n.text(
            "The scanmerge command is enabled. Open a new Terminal window. The link points to the current application location."
        )
    }
    @discardableResult func disable() throws -> String {
        guard let r = try receipt() else {
            return L10n.text("There is no command link installed by this application.")
        }
        let config = URL(fileURLWithPath: r.profile)
        let text = try readProfile(config)
        guard r.insertion.isEmpty || text.contains(r.insertion) else {
            throw Issue(
                message: L10n.text(
                    "The ScanMerge profile entry was edited manually. The modified configuration and link were not removed."
                ))
        }
        if exists(link), target() != r.target {
            throw Issue(
                message: L10n.text(
                    "The scanmerge link was replaced by another file. Neither that file nor the configuration was removed."
                ))
        }
        var newText = text
        if !r.insertion.isEmpty, let range = newText.range(of: r.insertion) {
            newText.removeSubrange(range)
        }
        let originalReceipt = try Data(contentsOf: receiptURL)
        let oldTarget = target()
        do {
            if newText != text { try writeProfile(newText, to: config) }
            if oldTarget != nil { try fm.removeItem(at: link) }
            try fm.removeItem(at: receiptURL)
            if r.profileWasMissing && newText.isEmpty { try fm.removeItem(at: config) }
        } catch {
            if !exists(link), let oldTarget {
                try? fm.createSymbolicLink(atPath: link.path, withDestinationPath: oldTarget)
            }
            try? writeProfile(text, to: config)
            try? originalReceipt.write(to: receiptURL, options: .atomic)
            throw error
        }
        return L10n.text(
            "Removed the link and PATH entry installed by ScanMerge. Other settings are unchanged.")
    }
}
