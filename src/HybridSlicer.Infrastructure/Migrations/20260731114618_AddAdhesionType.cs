using System;
using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace HybridSlicer.Infrastructure.Migrations
{
    /// <inheritdoc />
    public partial class AddAdhesionType : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<string>(
                name: "AdhesionType",
                table: "PrintJobs",
                type: "TEXT",
                nullable: false,
                defaultValue: "");

            migrationBuilder.AddColumn<int>(
                name: "BedIndex",
                table: "PrintJobs",
                type: "INTEGER",
                nullable: true);

            migrationBuilder.AddColumn<Guid>(
                name: "ParentJobId",
                table: "PrintJobs",
                type: "TEXT",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "SupportInfillDensityPct",
                table: "PrintJobs",
                type: "REAL",
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "SupportInfillPattern",
                table: "PrintJobs",
                type: "TEXT",
                nullable: false,
                defaultValue: "");

            migrationBuilder.AddColumn<double>(
                name: "BackBedEdgeOffsetMm",
                table: "MachineProfiles",
                type: "REAL",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<int>(
                name: "BedCount",
                table: "MachineProfiles",
                type: "INTEGER",
                nullable: false,
                defaultValue: 0);

            migrationBuilder.AddColumn<double>(
                name: "BedPositionXMm",
                table: "MachineProfiles",
                type: "REAL",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<double>(
                name: "BedPositionYMm",
                table: "MachineProfiles",
                type: "REAL",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<string>(
                name: "Beds",
                table: "MachineProfiles",
                type: "TEXT",
                nullable: false,
                defaultValue: "[]");

            migrationBuilder.AddColumn<string>(
                name: "CncAxes",
                table: "MachineProfiles",
                type: "TEXT",
                nullable: false,
                defaultValue: "");

            migrationBuilder.AddColumn<string>(
                name: "ExtruderAxes",
                table: "MachineProfiles",
                type: "TEXT",
                nullable: false,
                defaultValue: "");

            migrationBuilder.AddColumn<double>(
                name: "FrontBedEdgeOffsetMm",
                table: "MachineProfiles",
                type: "REAL",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<bool>(
                name: "MotionAssignmentEnabled",
                table: "MachineProfiles",
                type: "INTEGER",
                nullable: false,
                defaultValue: false);

            migrationBuilder.AddColumn<string>(
                name: "MotionAssignmentJson",
                table: "MachineProfiles",
                type: "TEXT",
                nullable: false,
                defaultValue: "");

            migrationBuilder.AddColumn<string>(
                name: "NozzleXOffsets",
                table: "MachineProfiles",
                type: "TEXT",
                nullable: false,
                defaultValue: "[]");

            migrationBuilder.AddColumn<string>(
                name: "OriginMode",
                table: "MachineProfiles",
                type: "TEXT",
                nullable: false,
                defaultValue: "BedCenter");

            migrationBuilder.AddColumn<double>(
                name: "OriginXMm",
                table: "MachineProfiles",
                type: "REAL",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<double>(
                name: "OriginYMm",
                table: "MachineProfiles",
                type: "REAL",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<double>(
                name: "TravelXMm",
                table: "MachineProfiles",
                type: "REAL",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<double>(
                name: "TravelYMm",
                table: "MachineProfiles",
                type: "REAL",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<double>(
                name: "TravelZMm",
                table: "MachineProfiles",
                type: "REAL",
                nullable: false,
                defaultValue: 0.0);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "AdhesionType",
                table: "PrintJobs");

            migrationBuilder.DropColumn(
                name: "BedIndex",
                table: "PrintJobs");

            migrationBuilder.DropColumn(
                name: "ParentJobId",
                table: "PrintJobs");

            migrationBuilder.DropColumn(
                name: "SupportInfillDensityPct",
                table: "PrintJobs");

            migrationBuilder.DropColumn(
                name: "SupportInfillPattern",
                table: "PrintJobs");

            migrationBuilder.DropColumn(
                name: "BackBedEdgeOffsetMm",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "BedCount",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "BedPositionXMm",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "BedPositionYMm",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "Beds",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "CncAxes",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "ExtruderAxes",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "FrontBedEdgeOffsetMm",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "MotionAssignmentEnabled",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "MotionAssignmentJson",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "NozzleXOffsets",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "OriginMode",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "OriginXMm",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "OriginYMm",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "TravelXMm",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "TravelYMm",
                table: "MachineProfiles");

            migrationBuilder.DropColumn(
                name: "TravelZMm",
                table: "MachineProfiles");
        }
    }
}
