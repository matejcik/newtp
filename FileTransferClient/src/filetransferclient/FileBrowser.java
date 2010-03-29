package filetransferclient;

import desktopsharingprotocol.DSPConnection;
import desktopsharingprotocol.DSPFile;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.ComponentEvent;
import java.awt.event.ComponentListener;
import java.net.InetAddress;
import java.util.*;
import javax.swing.*;
import javax.swing.event.TreeExpansionEvent;
import javax.swing.event.TreeSelectionEvent;
import javax.swing.event.TreeSelectionListener;
import javax.swing.event.TreeWillExpandListener;
import javax.swing.tree.*;

public class FileBrowser extends JFrame implements ActionListener {

	public static JTree tree;
	private JPanel panel;

	private DefaultTreeModel treeModel;

	public FileBrowser () {
		setTitle("File Browser");
		setSize(500, 500);
		setDefaultCloseOperation(EXIT_ON_CLOSE);

		TreeNode root = new DefaultMutableTreeNode("invisible root");
		treeModel = new DefaultTreeModel(root, true);
		tree = new JTree(treeModel);
		tree.setRootVisible(false);
		tree.getSelectionModel().setSelectionMode(TreeSelectionModel.SINGLE_TREE_SELECTION);
		tree.setCellRenderer(AnimatedTreeCellRenderer.getInstance());
		tree.addTreeWillExpandListener(new TreeWillExpandListener() {
			public void treeWillExpand(TreeExpansionEvent event) throws ExpandVetoException {
				if (!(event.getPath().getLastPathComponent() instanceof LoadableNode)) return;
				LoadableNode node = (LoadableNode)event.getPath().getLastPathComponent();
				new NodeExpander(node, tree, null, false).start();
			}

			public void treeWillCollapse(TreeExpansionEvent event) throws ExpandVetoException { }
		});
		tree.addTreeSelectionListener(new TreeSelectionListener() {
			public void valueChanged(TreeSelectionEvent e) {
				Object tn = e.getPath().getLastPathComponent();
				if (tn instanceof LoadableNode) {
					final LoadableNode ln = (LoadableNode)tn;
					new NodeExpander(ln, tree, new Runnable() {
						public void run() {
							if (ln instanceof DirectoryNode)
								fillRightPane((DirectoryNode)ln, true);
							else
								fillRightPane(ln, false);
						}
					}, false).start();
				}
			}
		});

		JScrollPane treeScroller = new JScrollPane(tree);
		treeScroller.setMinimumSize(new Dimension(150, 150));

		panel = new JPanel(new FlowLayout(FlowLayout.LEADING, 5, 5));
		final JScrollPane panelScroller = new JScrollPane(panel);
		panelScroller.addComponentListener(new ComponentListener() {
			public void componentResized(ComponentEvent e) {
				panel.setPreferredSize(new Dimension(panelScroller.getViewport().getWidth(), 1000));
				panel.revalidate();
			}

			public void componentMoved(ComponentEvent e) {}
			public void componentShown(ComponentEvent e) {}
			public void componentHidden(ComponentEvent e) {}
		});

		JSplitPane split = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, treeScroller, panelScroller);
		getContentPane().add(split);

		JMenuBar bar = new JMenuBar();
		setJMenuBar(bar);

		JMenu menu = new JMenu("File");
		JMenuItem mi;
		bar.add(menu);

		mi = new JMenuItem("Connect");
		mi.setActionCommand("connect");
		mi.addActionListener(this);
		menu.add(mi);

		menu.addSeparator();
		mi = new JMenuItem("Exit");
		mi.setActionCommand("exit");
		mi.addActionListener(this);
		menu.add(mi);
	}

	public static void main (String[] args) {
		FileBrowser fb = new FileBrowser();
		fb.setVisible(true);
	}

	public void actionPerformed(ActionEvent e) {
		if (e.getActionCommand() == "connect") {
			askForConnection();			
		} else if (e.getActionCommand() == "exit") {
			System.exit(0);
		}
	}

	private void askForConnection() {
		new Thread () {
			@Override public void run () {
				String response = (String)JOptionPane.showInputDialog(
					FileBrowser.this,
					"Enter host name", "connect",
					JOptionPane.QUESTION_MESSAGE, null,
					null, "");

				if (response == null) return;
				ConnectionNode dmt = new ConnectionNode(response);
				treeModel.insertNodeInto(dmt, (MutableTreeNode)treeModel.getRoot(),
					((DefaultMutableTreeNode)treeModel.getRoot()).getChildCount());
				tryConnectNode(dmt);

			}
		}.start();
	}

	private void tryConnectNode(final ConnectionNode dmt) {
		try {
			String host = dmt.getName();
			int sep = host.indexOf(':');
			int port = 39400;
			if (sep > -1) {
				port = Integer.valueOf(host.substring(sep + 1));
				host = host.substring(0, sep);
			}
			InetAddress ia = InetAddress.getByName(host);
			DSPConnection dc = new DSPConnection(host, port);
			dc.connect();
			dmt.setConnection(dc);
			
			final TreePath tp = new TreePath(treeModel.getPathToRoot(dmt));/*
			new NodeExpander(dmt, tree, new Runnable() {
				public void run() {
					fillRightPane(dmt, false);
					tree.expandPath(tp);
				}
			},true).run();*/
			SwingUtilities.invokeLater(new Runnable() {
				public void run() {
					//fillRightPane(dmt, false);
					tree.expandPath(tp);
				}
			});

		} catch (Exception e) {
			e.printStackTrace();
			JOptionPane.showMessageDialog(FileBrowser.this,
				e.toString(), "error while connecting",
				JOptionPane.ERROR_MESSAGE);
			treeModel.removeNodeFromParent(dmt);
		}
	}

	private void fillRightPane(LoadableNode node, boolean showParent) {
		panel.removeAll();
		if (showParent) {
			FileThumb ft = new FileThumb((LoadableNode)node.getParent());
			//ft.setText("..");
			panel.add(ft);
		}

		// fill directories, they have nodes
		Enumeration e = node.children();
		while (e.hasMoreElements()) {
			DirectoryNode dn = (DirectoryNode)e.nextElement();
			panel.add(new FileThumb(dn));
		}

		// fill files from node.files
		for (DSPFile file : node.files) {
			if (!file.isDirectory()) panel.add(new FileThumb(file));
		}

		panel.revalidate();
		panel.repaint();
	}
}
