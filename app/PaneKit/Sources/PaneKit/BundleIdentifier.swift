// Pane's identifier (ADR 0114), the reverse of its domain,
// pane.roundedcorners.xyz: its bundle's, its data root's and caches' folder
// names, its preferences' domain, and its log's subsystem. The engine's
// BRANDING (patch 0001) and app/project.yml name it too.
public enum BundleIdentifier {
	public static let pane = "xyz.roundedcorners.pane"
}
