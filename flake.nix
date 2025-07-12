{
  description = "Nix Flake for erlang_cnode PostgreSQL extension with PostgreSQL and Elixir dev environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        # Define the PostgreSQL version to use
        postgresql = pkgs.postgresql_17;
        postgresql_dev = pkgs.postgresql_17.pg_config;

        # Package the erlang_cnode extension
        erlang_cnode = pkgs.stdenv.mkDerivation {
          name = "erlang_cnode";
          src = ./.;
          buildInputs = [
            postgresql
            postgresql_dev
            pkgs.erlang
            pkgs.gcc
            pkgs.clang
          ];
          buildPhase = ''
            export ERL_INTERFACE_INCLUDE_DIR=${pkgs.erlang}/lib/erlang/usr/include
            export ERL_INTERFACE_LIB_DIR=${pkgs.erlang}/lib/erlang/usr/lib
            export PATH=${postgresql}/bin:${postgresql_dev}/bin:$PATH
            make
          '';
          installPhase = ''
            mkdir -p $out/{lib,share/postgresql/extension}
            cp *${postgresql.dlSuffix} $out/lib
            #cp *.sql $out/share/postgresql/extension TODO: fix this in Makefile
            cp erlang_cnode.sql $out/share/postgresql/extension/erlang_cnode--1.0.sql
            cp *.control $out/share/postgresql/extension
          '';
        };

        # Create a custom PostgreSQL package with the extension included
        postgresqlWithPlugins = pkgs.postgresql_17.withPackages (ps: [ erlang_cnode ]);

        # Script to initialize PostgreSQL and install the extension
        setupPostgresScript = pkgs.writeShellScriptBin "setup-postgres" ''
          # Initialize PostgreSQL data directory
          PGDATA=$HOME/pgdata
          if [ ! -d "$PGDATA" ]; then
            ${postgresqlWithPlugins}/bin/initdb -D $PGDATA
          fi

          # Start PostgreSQL
          ${postgresqlWithPlugins}/bin/pg_ctl -D $PGDATA -l $PGDATA/logfile start

          # Create database and install extension
          ${postgresqlWithPlugins}/bin/createdb testdb
        '';

        # Script to start PostgreSQL manually
        startPostgresScript = pkgs.writeShellScriptBin "start-postgres" ''
          PGDATA=$HOME/pgdata
          if [ ! -d "$PGDATA" ]; then
            echo "Initializing PostgreSQL data directory..."
            ${postgresqlWithPlugins}/bin/initdb -D $PGDATA
          fi
          echo "Starting PostgreSQL..."
          export HOME=$HOME
          ${postgresqlWithPlugins}/bin/pg_ctl -D $PGDATA -l $PGDATA/logfile start
          echo "PostgreSQL started. Use 'psql testdb' to connect."
        '';

        # Script to stop PostgreSQL manually
        stopPostgresScript = pkgs.writeShellScriptBin "stop-postgres" ''
          PGDATA=$HOME/pgdata
          echo "Stopping PostgreSQL..."
          ${postgresqlWithPlugins}/bin/pg_ctl -D $PGDATA stop
          echo "PostgreSQL stopped."
        '';

        # Script to create a test database
        createTestDbScript = pkgs.writeShellScriptBin "create-testdb" ''
          ${postgresqlWithPlugins}/bin/createdb testdb
          echo "Created testdb database."
        '';

        # Script to start an Elixir node for testing
        setupElixirScript = pkgs.writeShellScriptBin "setup-elixir" ''
          # Start an Elixir node with the specified name and cookie
          ${pkgs.elixir}/bin/iex --name testnode@localhost --cookie cookie123
        '';

        # Script to reset PostgreSQL data directory and use the latest build
        resetPostgresScript = pkgs.writeShellScriptBin "reset-postgres" ''
          PGDATA=$HOME/pgdata
          echo "Stopping PostgreSQL (if running)..."
          export HOME=$HOME
          ${postgresqlWithPlugins}/bin/pg_ctl -D $PGDATA stop || true
          echo "Removing old data directory..."
          rm -rf "$PGDATA"
          echo "Initializing new data directory..."
          ${postgresqlWithPlugins}/bin/initdb -D $PGDATA
          echo "Configuring PostgreSQL to use data directory for Unix sockets..."
          echo "unix_socket_directories = '$PGDATA'" >> $PGDATA/postgresql.conf
          echo "Reset complete. You can now run 'start-postgres' to start a fresh instance."
        '';

      in {
        packages = {
          erlang_cnode = erlang_cnode;
          postgresqlWithPlugins = postgresqlWithPlugins;
          default = erlang_cnode;
        };

        devShells.default = pkgs.mkShell {
          buildInputs = [
            postgresqlWithPlugins
            pkgs.erlang
            pkgs.elixir
            startPostgresScript
            stopPostgresScript
            createTestDbScript
            resetPostgresScript
            setupElixirScript
          ];
          shellHook = ''
            echo "Development environment for erlang_cnode"
            echo "PostgreSQL with plugins: ${postgresqlWithPlugins}/bin"
            echo "Elixir: ${pkgs.elixir}/bin/iex"
            echo ""
            echo "Available commands:"
            echo "  start-postgres    - Initialize and start PostgreSQL"
            echo "  stop-postgres     - Stop PostgreSQL"
            echo "  create-testdb     - Create testdb database"
            echo "  psql testdb       - Connect to testdb"
            echo "  setup-elixir      - Start an Elixir node for testing"
            echo "  reset-postgres    - Reset data directory and re-init PostgreSQL"
            echo ""
            export PGDATA=$HOME/pgdata
            export PGHOST=localhost
            export PGUSER=$USER
            export PATH=${postgresqlWithPlugins}/bin:$PATH
          '';
        };
      }
    );
}
