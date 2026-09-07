// SPDX-License-Identifier: GPL-3.0-only

import Foundation

@main struct LocalizationTests {
    static func main() throws {
        let fm = FileManager.default
        let source = URL(fileURLWithPath: fm.currentDirectoryPath).appendingPathComponent(
            "gui/Resources")
        let temp = fm.temporaryDirectory.appendingPathComponent(
            "scanmerge-l10n-" + UUID().uuidString)
        defer { try? fm.removeItem(at: temp) }
        let contents = temp.appendingPathComponent("Tests.bundle/Contents")
        let resources = contents.appendingPathComponent("Resources")
        try fm.createDirectory(at: resources, withIntermediateDirectories: true)
        let info: [String: Any] = [
            "CFBundleIdentifier": "local.scanmerge.localization-tests",
            "CFBundleDevelopmentRegion": "en", "CFBundleLocalizations": ["en", "pl"],
        ]
        try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0).write(
            to: contents.appendingPathComponent("Info.plist"))
        var catalogs: [[String: String]] = []
        for language in ["en", "pl"] {
            try fm.copyItem(
                at: source.appendingPathComponent(language + ".lproj"),
                to: resources.appendingPathComponent(language + ".lproj"))
            let data = try Data(
                contentsOf: source.appendingPathComponent(language + ".lproj/Localizable.strings"))
            catalogs.append(
                try PropertyListSerialization.propertyList(from: data, format: nil)
                    as! [String: String])
        }
        precondition(Set(catalogs[0].keys) == Set(catalogs[1].keys))
        let pattern = try NSRegularExpression(pattern: "%(@|ld)")
        for (key, english) in catalogs[0] {
            let polish = catalogs[1][key]!
            precondition(!polish.isEmpty)
            func placeholders(_ value: String) -> [String] {
                pattern.matches(in: value, range: NSRange(value.startIndex..., in: value)).map {
                    (value as NSString).substring(with: $0.range)
                }
            }
            precondition(placeholders(english) == placeholders(polish), "Format mismatch: " + key)
        }
        for (preferences, expected) in [
            (["pl"], "pl"), (["pl-PL"], "pl"), (["pl_PL"], "pl"), (["PL"], "pl"),
            (["en", "pl"], "en"), (["de-DE", "pl"], "en"), (["fr"], "en"), ([], "en"),
        ] {
            precondition(L10n.language(for: preferences) == expected)
        }
        L10n.resources = Bundle(url: contents.deletingLastPathComponent())!
        let expectedLanguage = CommandLine.arguments.contains("(pl)") ? "pl" : "en"
        precondition(L10n.language == expectedLanguage)
        let polish = L10n.language == "pl"
        precondition(L10n.text("Ready") == (polish ? "Gotowy do pracy" : "Ready"))
        precondition(
            L10n.text("Capture %ld of %ld", 2, 9) == (polish ? "Ujęcie 2 z 9" : "Capture 2 of 9"))
        precondition(
            L10n.text(
                "Another file or link already exists at %@. It will not be overwritten.", "/test"
            ).contains("/test"))
        precondition(L10n.text("Unknown test key") == "Unknown test key")
        print(
            "PASS: \(catalogs[0].count) translations, placeholders, primary-language selection, localized runtime strings (\(L10n.language))"
        )
    }
}
