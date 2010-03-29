package filetransferclient;

import java.awt.Component;
import java.awt.Image;
import java.awt.Rectangle;
import java.awt.image.ImageObserver;
import javax.swing.ImageIcon;
import javax.swing.JLabel;
import javax.swing.JTree;
import javax.swing.tree.DefaultTreeCellRenderer;

public class AnimatedTreeCellRenderer extends DefaultTreeCellRenderer {

	private static AnimatedTreeCellRenderer instance;

	private AnimatedTreeCellRenderer () {
		ImageIcon folder = new ImageIcon(getClass().getResource("/images/folder-small.png"));
		setClosedIcon(folder);
		setOpenIcon(folder);
	}

	public static AnimatedTreeCellRenderer getInstance () {
		if (instance == null)
			instance = new AnimatedTreeCellRenderer();
		return instance;
	}

	@Override
	public Component getTreeCellRendererComponent(final JTree tree, Object value,
			boolean sel, boolean expanded, boolean leaf, int row, boolean hasFocus) {

		JLabel label = (JLabel)super.getTreeCellRendererComponent(tree, value, sel, expanded, leaf, row, hasFocus);

		if (value instanceof LoadableNode) {
			final LoadableNode val = (LoadableNode)value;
			ImageIcon icon = val.getCustomIcon();
			if (icon != null) label.setIcon(icon);
		}
		return label;
	}
}
